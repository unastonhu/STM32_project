/*
*
*@file    flash_manager.h
*
*@brief   W25Q64 边缘微型数据库 (系统配置 / AI参考集 / 128条环形日志)
*/

#ifndef __FLASH_MANAGER_H
#define __FLASH_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "enose.h"

// ==========================================
// Flash 内存映射表 (基于 4KB 扇区擦除)
// ==========================================
#define FLASH_ADDR_SYS_CONFIG  0x000000    // Sector 0: 系统核心与BSEC状态
#define FLASH_ADDR_AI_REF      0x001000    // Sector 1: AI 动态参考集 (最大 32 条)
#define FLASH_ADDR_LOG_BASE    0x002000    // Sector 2-3: 128条环形日志区

#define MAX_FLASH_LOGS         128         // 日志缓存最大记录数 (2个扇区)
#define LOG_ENTRY_SIZE         64          // 单条日志大小严格锁定 64 字节

/*
 * Flash 2 is reserved for the current boot's synchronized 1 Hz history.
 * A 2 MB area stores 65536 records, or about 18.2 hours at 1 Hz.
 * The W25Q64 sanity-check sector at the end of the chip is untouched.
 */
#define FLASH_HISTORY_DEV_INDEX     1U
#define FLASH_HISTORY_ADDR_BASE     0x000000U
#define FLASH_HISTORY_AREA_SIZE     (2U * 1024U * 1024U)
#define FLASH_HISTORY_RECORD_SIZE   32U
#define MAX_FLASH_HISTORY_RECORDS   (FLASH_HISTORY_AREA_SIZE / FLASH_HISTORY_RECORD_SIZE)

#define MAGIC_SYS_CONFIG       0xAA55AA55
#define MAGIC_AI_REF           0xBB66BB66

// ==========================================
// 数据结构定义
// ==========================================

// 1. 系统掉电记忆区 (Sector 0)
typedef struct {
uint32_t magic;
uint32_t boot_counter;        // 系统累计开机次数
float    weight_anchor_g;     // 天平系统的稳定重量基准
uint8_t  bsec_state_len;      // BSEC 状态数组长度 (通常 139)
uint8_t  bsec_state[140];     // BSEC 算法环境底噪记忆
uint8_t  padding[103];        // 凑满256字节(1页)对齐
} FlashSysConfig_t;

// 2. 64字节定长日志实体 (Sector 2~3)
typedef struct {
uint32_t timestamp;           // [4B] UNIX 时间戳
float    resp[ENOSE_NUM_CH];  // [20B] 5 通道归一化响应特征
float    env_temp;            // [4B] 温度
float    env_hum;             // [4B] 湿度
float    spoilage_risk;       // [4B] BME688 腐败率
float    weight_loss_g;       // [4B] 异常失水量
uint8_t  ai_state;            // [1B] 记录时的分类状态
uint8_t  padding[23];         // [23B] 占位补齐至 64B
} FlashLogEntry_t;

/* Flash 2: fixed-size synchronized raw history record. */
typedef struct {
uint32_t timestamp;           // Local/uptime seconds from SysTime
uint32_t uptime_ms;           // HAL tick for ordering within this boot
float    raw[ENOSE_NUM_CH];   // SGP40, TVOC, eCO2, BME688 gas, HX711
uint8_t  valid_mask;
uint8_t  door_state;
uint16_t checksum;
} FlashHistoryRecord_t;

// 3. AI 参考集区 (Sector 1)
typedef struct {
uint32_t magic;
ENose_ClassRef_t classes[ENOSE_NUM_CLASS]; // 32个特征槽位
} FlashRefConfig_t;

// ==========================================
// 全局运行状态
// ==========================================
typedef struct {
uint32_t head_index; // 下一个要写入的日志索引 (0 ~ 127)
uint32_t log_count;  // 当前有效的日志总数 (最大 128)
} FlashLogState_t;

extern FlashLogState_t g_flash_log;

typedef struct {
uint32_t head_index;
uint32_t record_count;
} FlashHistoryState_t;

extern FlashHistoryState_t g_flash_history;

// ==========================================
// API 接口
// ==========================================

void FlashMgr_Init(void);

// 系统核心状态存取
void FlashMgr_SaveSysState(uint8_t *bsec_state, uint8_t bsec_len, float current_weight_anchor);
bool FlashMgr_LoadSysState(uint8_t *bsec_state, uint8_t *bsec_len, float *weight_anchor);

// AI 参考集存取与管控
void FlashMgr_SaveEnoseClasses(const ENose_t *e);
void FlashMgr_LoadEnoseClasses(ENose_t *e);
bool FlashMgr_DeleteReference(uint8_t ref_id, ENose_t *e);
bool FlashMgr_ModifyReferenceLabel(uint8_t ref_id, uint8_t new_label, ENose_t *e);

// 环形日志存取
void FlashMgr_AppendLog(const ENose_t *e, float temp, float hum, float risk, float loss);
void FlashMgr_AppendLogEntry(const FlashLogEntry_t *entry);
bool FlashMgr_ReadLog(uint32_t offset_from_newest, FlashLogEntry_t *out_entry);
void FlashMgr_ClearLogs(void);

// Current-boot synchronized history on Flash 2
void FlashMgr_HistorySessionInit(void);
bool FlashMgr_AppendHistoryBatch(const FlashHistoryRecord_t *records, uint8_t count);

// 提拔机制
bool FlashMgr_PromoteLogToRef(uint32_t log_offset, uint8_t target_ref_id, ENose_t *e);

#endif /* __FLASH_MANAGER_H */
