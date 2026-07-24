/*
*
*enose.c  —  电子鼻感知核心实现
*
*纯 C99，无外部依赖（只用 <string.h>）。所有距离计算用平方欧氏，避免 sqrt。
*/

#include "enose.h"
#include "system_data.h"
#include <string.h>
#include <math.h>

extern SystemData_t sysData; // 引用全局数据中枢

/* ============ 可调参数（按你的箱体/传感器改） ============ */
#define WARMUP_MS        180000u  // MOX 预热时长，MOX 一般需数分钟才稳定 
#define BASELINE_MS       30000u  // 采清洁空气基线的时长 
#define BASELINE_MIN_N       10   // 基线至少要采到的样本数 

#define CLEAN_RESP_TH      0.05f  // 判定"当前空气干净"的响应上限 
#define BASE_DRIFT_ALPHA  0.001f  // 干净时基线自适应速度（很慢，防漂移） 

#define D_VOC_WARN         0.20f  /* VOC 每分钟涨幅超此值 → 早期预警加分 */
#define WARN_BONUS         0.15f

/* 未示教时的兜底阈值（变质分数 → 状态） */
#define TH_RIPENING        0.25f
#define TH_OVERRIPE        0.50f
#define TH_SPOILED         0.75f

/* ============ 小工具 ============ */
static inline float clampf(float v, float lo, float hi) {
return v < lo ? lo : (v > hi ? hi : v);
}

/* ============ 计算蒸气压差 (VPD, Vapor Pressure Deficit) ============ */
float ENose_CalculateVPD(float temp_c, float rh_pct)
{
if (rh_pct < 1.0f) rh_pct = 1.0f;
if (rh_pct > 100.0f) rh_pct = 100.0f;

// 1. 饱和蒸气压 e_s (kPa)
float es = 0.61078f * expf((17.27f * temp_c) / (temp_c + 237.3f));
// 2. 实际蒸气压 e_a (kPa)
float ea = es * (rh_pct / 100.0f);
// 3. VPD = e_s - e_a
float vpd = es - ea;
return (vpd < 0.0f) ? 0.0f : vpd;


}

/* ============ 初始化 ============ */
void ENose_Init(ENose_t *e)
{
memset(e, 0, sizeof(*e));

/* ---- 默认配置：务必按实际传感器修改！ ----
 * polarity: 让"目标越多 → 响应越大"。
 *   SGP40 SRAW 随 VOC 上升而下降 → -1（若用别的量取向就改符号）
 *   TVOC / eCO2 随污染上升 → +1
 *   BME688 gas_res 随还原气上升而下降 → -1
 *   weight 随失水下降 → -1
 * scale: 该通道"明显变质"时相对基线的原始偏移量（决定归一化尺度）
 * weight: 规则兜底判据的权重，和为 1
 */

float def_pol[ENOSE_NUM_CH]   = { -1.f, +1.f, +1.f, -1.f, -1.f };
float def_scale[ENOSE_NUM_CH] = { 8000.f, 2000.f, 2000.f, 40000.f, 1.f };
float def_w[ENOSE_NUM_CH]     = { 0.30f, 0.20f, 0.15f, 0.25f, 0.10f };
/* 注：scale[4] 失重请填 初始重量 × 允许失重率，例如 W0=200g、15% → 30.f */

memcpy(e->polarity, def_pol,   sizeof(def_pol));
memcpy(e->scale,    def_scale, sizeof(def_scale));
memcpy(e->weight,   def_w,     sizeof(def_w));

e->mode  = MODE_WARMUP;
e->state = ENOSE_UNKNOWN;

// 门控状态机初始化
e->door_state = DOOR_STATE_CLOSED_MONITORING;
e->last_ir_status = 1; // 默认关门


}

void ENose_AttachWeightEngine(ENose_t *e, FridgeWeightEngine_t *w_eng)
{
e->weight_engine = w_eng;
}

void ENose_SetMode(ENose_t *e, ENose_Mode_t m, uint32_t now_ms)
{
e->mode = m;
e->mode_since_ms = now_ms;
if (m == MODE_BASELINE) {
e->bl_cnt = 0;
memset(e->base, 0, sizeof(e->base));
}
}

void ENose_RecaptureBaseline(ENose_t *e, uint32_t now_ms)
{
ENose_SetMode(e, MODE_BASELINE, now_ms);
}

/* ============ 特征提取 ============ /
/ raw → 归一化响应 resp[0..1]，并算 VOC 斜率 */
static void compute_features(ENose_t *e, const float raw[ENOSE_NUM_CH], float dt_min)
{
for (int i = 0; i < ENOSE_NUM_CH; ++i) {
float r = e->polarity[i] * (raw[i] - e->base[i]) / e->scale[i];
e->resp[i] = clampf(r, 0.f, 1.f);
e->last_raw[i] = raw[i];
}
float voc = e->resp[0];
if (dt_min > 1e-4f)
e->d_voc_dt = (voc - e->prev_voc) / dt_min;
e->prev_voc = voc;
}

/* 门控自适应基线：仅当"当前空气干净"时才极缓慢地跟随，

从而修正长期漂移，又绝不会把真正的持续升高（腐烂）当成基线归零。

—— 这一步专门解决 MOX / SGP40 自动基线"把慢腐烂归一化掉"的坑。 */
static void adapt_baseline(ENose_t *e, const float raw[ENOSE_NUM_CH])
{
float maxr = 0.f;
for (int i = 0; i < ENOSE_NUM_CH; ++i)
if (e->resp[i] > maxr) maxr = e->resp[i];

if (maxr < CLEAN_RESP_TH) {              /* 判定为干净 */
for (int i = 0; i < ENOSE_NUM_CH; ++i)
e->base[i] += (raw[i] - e->base[i]) * BASE_DRIFT_ALPHA;
}
}

/* ============ 融合：规则变质指数 ============ */
static float rule_index(const ENose_t *e)
{
float s = 0.f;
for (int i = 0; i < ENOSE_NUM_CH; ++i)
s += e->weight[i] * e->resp[i];
if (e->d_voc_dt > D_VOC_WARN)            // 斜率突增 = 发酵/腐败启动 
s += WARN_BONUS;
return clampf(s, 0.f, 1.f);
}

/* ============ 分类：示教式最近邻中心 ============ */
static int taught_class_count(const ENose_t *e)
{
int n = 0;
for (int i = 0; i < ENOSE_NUM_CLASS; ++i)
if (e->cls[i].valid && e->cls[i].n_samples > 0) ++n;
return n;
}

/* 返回与当前响应最近的已示教类别；无有效类别返回 UNKNOWN */
static ENose_State_t nearest_centroid(const ENose_t *e)
{
int best = -1;
float best_d2 = 0.f;
for (int c = 0; c < ENOSE_NUM_CLASS; ++c) {
if (!(e->cls[c].valid && e->cls[c].n_samples > 0)) continue;
float d2 = 0.f;
for (int i = 0; i < ENOSE_NUM_CH; ++i) {
float d = e->resp[i] - e->cls[c].centroid[i];
d2 += d * d;
}
if (best < 0 || d2 < best_d2) { best_d2 = d2; best = c; }
}
return (best < 0) ? ENOSE_UNKNOWN : (ENose_State_t)best;
}

/* 兜底：未示教足够类别时，用变质指数走阈值 */
static ENose_State_t index_to_state(float idx)
{
if (idx < TH_RIPENING) return ENOSE_FRESH;
if (idx < TH_OVERRIPE) return ENOSE_RIPENING;
if (idx < TH_SPOILED)  return ENOSE_OVERRIPE;
return ENOSE_SPOILED;
}

/* ============ 主循环 ============ */
ENose_State_t ENose_Tick(ENose_t *e, const float raw[ENOSE_NUM_CH],
uint32_t now_ms, float dt_min)
{
uint8_t cur_ir = sysData.ir.status; // 0: 开门, 1: 关门
uint32_t now_sec = now_ms / 1000;

// =========================================================================
// 1. 物理门控状态转换逻辑 (Door-State Machine)
// =========================================================================
if (cur_ir == 0) {
    // [开门状态]: 只要 IR 检测到开门，立刻挂起 AI 判定
    e->door_state = DOOR_STATE_OPEN;
    e->last_ir_status = 0;
    e->state = ENOSE_UNKNOWN;
    return e->state;
}
else if (e->last_ir_status == 0 && cur_ir == 1) {
    // [检测到关门瞬间 (上升沿)]: 进入 180 秒 Recovery 恢复期
    e->door_state = DOOR_STATE_RECOVERY;
    e->recovery_timer_sec = 180;
}
e->last_ir_status = cur_ir;

// 关门恢复期倒计时处理
if (e->door_state == DOOR_STATE_RECOVERY) {
    if (e->recovery_timer_sec > 0) {
        e->recovery_timer_sec--;
        e->state = ENOSE_UNKNOWN;
        return e->state;
    } else {
        // 倒计时结束，触发天平关门自愈校准
        if (e->weight_engine) {
            FridgeWeight_SelfHeal(e->weight_engine, e->weight_engine->current_filtered_w);
        }
        e->door_state = DOOR_STATE_CLOSED_MONITORING;
    }
}

// =========================================================================
// 2. 电子鼻主生命周期状态机 (MODE_WARMUP -> BASELINE -> RUN)
// =========================================================================
uint32_t elapsed = now_ms - e->mode_since_ms;

switch (e->mode) {

case MODE_WARMUP:
    if (elapsed >= WARMUP_MS)
        ENose_SetMode(e, MODE_BASELINE, now_ms);
    e->state = ENOSE_UNKNOWN;
    return e->state;

case MODE_BASELINE:
    /* 增量平均求清洁空气基线 */
    for (int i = 0; i < ENOSE_NUM_CH; ++i)
        e->base[i] += (raw[i] - e->base[i]) / (float)(e->bl_cnt + 1);
    e->bl_cnt++;
    if (elapsed >= BASELINE_MS && e->bl_cnt >= BASELINE_MIN_N) {
        /* 基线就绪，进入全自动运行 */
        e->prev_voc = 0.f;
        ENose_SetMode(e, MODE_RUN, now_ms);
    }
    e->state = ENOSE_UNKNOWN;
    return e->state;

case MODE_LEARN:
    /* 只算特征（供 ENose_TeachCurrent 取用），执行器冻结、状态不变 */
    compute_features(e, raw, dt_min);
    e->index = rule_index(e);
    return e->state;

case MODE_RUN:
case MODE_LOG:
default:
    compute_features(e, raw, dt_min);
    adapt_baseline(e, raw);
    e->index = rule_index(e);

    ENose_State_t calculated_state;
    if (taught_class_count(e) >= 2) {
        ENose_State_t s = nearest_centroid(e);
        calculated_state = (s == ENOSE_UNKNOWN) ? index_to_state(e->index) : s;
    } else {
        calculated_state = index_to_state(e->index);   /* 还没教够 → 走指数阈值 */
    }

    // 多模态交叉验证确诊逻辑 (结合 VPD 失水倍率与 BSEC2 气味 Risk)
    if (e->weight_engine && e->weight_engine->active_count > 0) {
        // 计算 VPD 蒸气压差
        float t_env = sysData.env.aht_temp;
        float h_env = sysData.env.aht_hum;
        e->current_vpd_kpa = ENose_CalculateVPD(t_env, h_env);

        // 计算实际物理总失重
        float anchor_w = e->weight_engine->base_anchor_weight;
        float cur_w = e->weight_engine->current_filtered_w;
        e->actual_total_loss_g = (anchor_w > cur_w) ? (anchor_w - cur_w) : 0.0f;

        // 基于 VPD 动态遍历虚拟阵列计算理论正常失水量
        float allowed_loss_sum = 0.0f;
        for (int i = 0; i < MAX_VIRTUAL_ITEMS; i++) {
            if (e->weight_engine->items[i].is_active) {
                float elapsed_days = (float)(now_sec - e->weight_engine->items[i].put_in_time) / 86400.0f;
                if (elapsed_days < 0.001f) elapsed_days = 0.001f;

                float vpd_factor = e->current_vpd_kpa / 0.16f; // 标准环境基准 0.16kPa
                if (vpd_factor < 0.5f) vpd_factor = 0.5f;
                if (vpd_factor > 3.0f) vpd_factor = 3.0f;

                float item_loss = e->weight_engine->items[i].initial_weight * BASE_DAILY_LOSS * elapsed_days * vpd_factor;
                allowed_loss_sum += item_loss;
            }
        }
        e->allowed_normal_loss_g = (allowed_loss_sum < 0.5f) ? 0.5f : allowed_loss_sum;
        e->abnormal_loss_ratio = e->actual_total_loss_g / e->allowed_normal_loss_g;

        // 多模态交叉判定
        float bme_spoilage_risk = sysData.bme688.food_spoilage_risk; // 0.0 ~ 1.0

        if (e->abnormal_loss_ratio > 1.8f && bme_spoilage_risk > 0.60f) {
            // 【确诊腐败】：失重超标 1.8 倍且 BME688 异味狂响！
            calculated_state = ENOSE_SPOILED;
            sysData.relays.ozone = 1;         // 启动臭氧杀菌
            sysData.relays.duct_fans[0] = 1;  // 启动风机排风
        } else {
            sysData.relays.ozone = 0;
        }
    }

    e->state = calculated_state;
    return e->state;
}


}

/* ============ 示教 ============ */
void ENose_TeachCurrent(ENose_t *e, ENose_State_t label)
{
if (label < 0 || label >= ENOSE_NUM_CLASS) return;
ENose_ClassRef_t *c = &e->cls[label];

/* 用当前响应对该类别中心做增量平均 */
for (int i = 0; i < ENOSE_NUM_CH; ++i)
    c->centroid[i] += (e->resp[i] - c->centroid[i]) / (float)(c->n_samples + 1);
c->n_samples++;
c->valid = 1;


}

void ENose_ForgetClass(ENose_t *e, ENose_State_t label)
{
if (label < 0 || label >= ENOSE_NUM_CLASS) return;
memset(&e->cls[label], 0, sizeof(e->cls[label]));
}
