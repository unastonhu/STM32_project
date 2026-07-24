/*
*
*enose.h  —  电子鼻感知核心（硬件无关，纯 C99）
*
*设计目标（面向"产品化"）：
*
*全自动运行：上电走 预热 → 采基线 → 运行 状态机，无需人工干预
*
*半自动标定：用"示教式"学习——放个样品按一下键说明它是哪一类，
*
*设备自己记住该类别的气味指纹（特征中心），自动算出判定边界，
*
*
*不需要你手填任何阈值/权重
*
*
*与硬件解耦：本模块不碰寄存器/HAL，你只需在外部接三个口：
*

读传感器：把 5 路原始读数按顺序喂给 ENose_Tick()

存 Flash ：ENose_t 是 POD，直接 memcpy 保存 base[] 和 cls[]

驱动执行：根据 ENose_Tick() 返回的状态去开关你的继电器

*5 个通道（顺序固定，喂 raw[] 时对齐）：
*
*[0] SGP40   VOC   (建议用原始 SRAW，别用会自适应归零的 VOC Index)
*
*[1] ENS160 TVOC
*
*[2] ENS160 eCO2  (呼吸强度代理，非真 CO2)
*
*[3] BME688 gas_res
*
*[4] HX711  weight (失重通道，慢)
*/
#ifndef ENOSE_H
#define ENOSE_H

#include <stdint.h>
#include "fridge_weight_engine.h"

#define ENOSE_NUM_CH     5   // 传感器通道数
#define ENOSE_NUM_CLASS  32  // 类别槽位数 (扩容至 32 条以供参考集与 CubeAI 联动)

/* 判定状态（同时用作示教时的类别下标 0..3） */
typedef enum {
ENOSE_FRESH    = 0,
ENOSE_RIPENING = 1,
ENOSE_OVERRIPE = 2,
ENOSE_SPOILED  = 3,
ENOSE_UNKNOWN  = -1   // 预热/采基线阶段，读数不可信
} ENose_State_t;

/* 运行模式 */
typedef enum {
MODE_WARMUP = 0,   // MOX 预热中，忽略读数
MODE_BASELINE,     // 采清洁空气基线（放样品前跑）
MODE_RUN,          // 全自动：融合 → 判定 → 由外部驱动执行器
MODE_LEARN,        // 半自动：示教打标，积累类别中心（执行器冻结）
MODE_LOG           // 记录特征到 SD，供离线训练（可选）
} ENose_Mode_t;

/* 门控状态机枚举 (解决开门突变与关门稳定) */
typedef enum {
DOOR_STATE_CLOSED_MONITORING = 0, // 关门稳定监控期 (正常跑 AI 与失水评估)
DOOR_STATE_OPEN              = 1, // 开门期 (挂起 AI，记录重量阶跃动作)
DOOR_STATE_RECOVERY          = 2  // 关门恢复期 (倒计时 wait 3分钟，等待气流与秤盘稳定)
} ENose_DoorState_t;

/* 单个类别的"气味指纹中心"（示教学到的特征均值） */
typedef struct {
float    centroid[ENOSE_NUM_CH]; // 该类别的平均归一化响应
uint16_t n_samples;              // 已示教样本数，0 = 还没教过
uint8_t  valid;                  // 1 = 该类别可用
uint8_t  mapped_label;           // 映射标签 (0:新鲜 1:成熟 2:过熟 3:腐败 等)
} ENose_ClassRef_t;

typedef struct {
/* ===== 需要你按所用传感器填的静态配置 ===== */
float polarity[ENOSE_NUM_CH]; // +1/-1：使"目标气体越多 → 响应越大"
float scale[ENOSE_NUM_CH];    // 每通道量程：约等于"明显变质"时的原始偏移量
float weight[ENOSE_NUM_CH];   // 规则指数权重（未示教时的兜底判据，和为 1）

/* ===== 运行时状态（模块内部维护，不用管） ===== */
float base[ENOSE_NUM_CH];     // 清洁空气基线（自动采集 + 门控自适应）
float resp[ENOSE_NUM_CH];     // 当前归一化响应 0..1
float last_raw[ENOSE_NUM_CH]; // 上一拍原始值
float prev_voc;               // 上一拍 VOC，用于算斜率
float d_voc_dt;               // VOC 变化率（/分钟），早期预警用

ENose_ClassRef_t cls[ENOSE_NUM_CLASS];

ENose_Mode_t  mode;
ENose_State_t state;          // 当前判定结果
float         index;          // 融合变质分数 0..1
uint16_t      bl_cnt;         // 基线采样计数（内部用）
uint32_t      mode_since_ms;  // 进入当前模式的时刻

/* ===== [新增扩展]：门控状态机与 VPD 失水评估引擎字段 ===== */
ENose_DoorState_t    door_state;             // 当前物理门控状态
uint8_t              last_ir_status;         // 上一帧 IR 门磁状态 (0:开, 1:关)
uint32_t             recovery_timer_sec;     // 关门恢复期倒计时 (秒)
float                current_vpd_kpa;        // 基于 AHT21 计算的蒸气压差 (VPD, kPa)
float                actual_total_loss_g;    // 物理累积总失重量 (g)
float                allowed_normal_loss_g;  // 理论容许的最大正常失重量 (g)
float                abnormal_loss_ratio;    // 异常失水倍率 (实际失重 / 理论失重)
FridgeWeightEngine_t *weight_engine;         // 指向外部重量引擎的指针

} ENose_t;

/* ---- 生命周期 ---- */

/* 初始化：清留 + 载入默认参数，进入 MODE_WARMUP。

调用后请自行覆盖 e->polarity / e->scale / e->weight（见 .c 顶部示例）。 */
void ENose_Init(ENose_t *e);

/* 绑定外部重量处理引擎 */
void ENose_AttachWeightEngine(ENose_t *e, FridgeWeightEngine_t *w_eng);

/* 切换模式（也可让状态机自动流转） */
void ENose_SetMode(ENose_t *e, ENose_Mode_t m, uint32_t now_ms);

/* ---- 主循环 ---- */

/* 每个采样周期调一次。

raw    : 5 路原始读数，顺序同 ENOSE_NUM_CH 注释

now_ms : 当前系统时间（ms，用 FreeRTOS Tick 即可）

dt_min : 距上一拍的时间间隔（分钟），用于算变化率

返回：当前判定状态（WARMUP/BASELINE 阶段返回 ENOSE_UNKNOWN）。 */
ENose_State_t ENose_Tick(ENose_t *e, const float raw[ENOSE_NUM_CH],
uint32_t now_ms, float dt_min);

/* ---- 半自动标定的核心 ---- */

/* 示教：把"当前这一拍"标注成某个类别，一次按键调一次。

内部对该类别中心做增量平均——多教几次会更准。

教满 >=2 个类别后，判定自动从"阈值兜底"切换到"最近邻分类"。 */
void ENose_TeachCurrent(ENose_t *e, ENose_State_t label);

/* 清空某个已示教类别（教错了想重来时用） */
void ENose_ForgetClass(ENose_t *e, ENose_State_t label);

/* 手动触发重采基线（等价于切到 MODE_BASELINE） */
void ENose_RecaptureBaseline(ENose_t *e, uint32_t now_ms);

/* ---- 辅助功能 ---- */
float ENose_CalculateVPD(float temp_c, float rh_pct);

#endif /* ENOSE_H */
