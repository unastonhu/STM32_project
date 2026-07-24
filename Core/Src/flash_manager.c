/*
*
*@file    flash_manager.c
*
*@brief   W25Q64 边缘微型数据库实现 (精确匹配 w25q64.h API)
*/

#include "flash_manager.h"
#include "w25q64.h"
#include "sys_time.h"
#include <string.h>

#define FLASH_DEV_INDEX 0 // 默认使用 1 号芯片 (dev_index = 0)

FlashLogState_t g_flash_log = {0};
FlashHistoryState_t g_flash_history = {0};

_Static_assert(sizeof(FlashLogEntry_t) == LOG_ENTRY_SIZE,
               "FlashLogEntry_t must remain 64 bytes");
_Static_assert(sizeof(FlashHistoryRecord_t) == FLASH_HISTORY_RECORD_SIZE,
               "FlashHistoryRecord_t must remain 32 bytes");

// --- 内部辅助函数：自动跨页写 (解决 W25Q64_WritePage 单次最大 256 字节限制) ---
static void Flash_WriteBuffer(uint32_t write_addr, uint8_t* pBuffer, uint16_t num_bytes)
{
uint16_t pageremain = 256 - (write_addr % 256);
if(num_bytes <= pageremain) pageremain = num_bytes;

while(1) {
    W25Q64_WritePage(FLASH_DEV_INDEX, pBuffer, write_addr, pageremain);
    if(num_bytes == pageremain) break; 
    pBuffer += pageremain;
    write_addr += pageremain;
    num_bytes -= pageremain;
    pageremain = (num_bytes > 256) ? 256 : num_bytes;
}


}

void FlashMgr_Init(void)
{
// 扫描 128 条日志，寻找最新时间戳
uint32_t max_ts = 0;
int32_t max_idx = -1;
uint32_t valid_cnt = 0;

for (int i = 0; i < MAX_FLASH_LOGS; i++) {
    uint32_t ts;
    W25Q64_ReadData(FLASH_DEV_INDEX, (uint8_t*)&ts, FLASH_ADDR_LOG_BASE + i * LOG_ENTRY_SIZE, 4);
    
    if (ts != 0xFFFFFFFF) { 
        valid_cnt++;
        if (ts >= max_ts) {
            max_ts = ts;
            max_idx = i;
        }
    }
}

if (max_idx == -1) {
    g_flash_log.head_index = 0;
    g_flash_log.log_count = 0;
} else {
    g_flash_log.head_index = (max_idx + 1) % MAX_FLASH_LOGS;
    g_flash_log.log_count = valid_cnt;
}


}

void FlashMgr_SaveSysState(uint8_t *bsec_state,uint8_t bsec_len,float current_weight_anchor)
{
    FlashSysConfig_t cfg = {0};

    W25Q64_ReadData(
        FLASH_DEV_INDEX,
        (uint8_t *)&cfg.boot_counter,
        FLASH_ADDR_SYS_CONFIG + 4,
        sizeof(cfg.boot_counter)
    );

    if (cfg.boot_counter == 0xFFFFFFFF) {
        cfg.boot_counter = 0;
    }

    cfg.magic = MAGIC_SYS_CONFIG;
    cfg.boot_counter++;
    cfg.weight_anchor_g = current_weight_anchor;

    if (bsec_state != NULL && bsec_len <= sizeof(cfg.bsec_state)) {
        cfg.bsec_state_len = bsec_len;
        memcpy(cfg.bsec_state, bsec_state, bsec_len);
    }

    W25Q64_EraseSector(
        FLASH_DEV_INDEX,
        FLASH_ADDR_SYS_CONFIG / 4096
    );

    Flash_WriteBuffer(
        FLASH_ADDR_SYS_CONFIG,
        (uint8_t *)&cfg,
        sizeof(cfg)
    );
}

bool FlashMgr_LoadSysState(uint8_t *bsec_state,uint8_t *bsec_len,float *weight_anchor)
{
    FlashSysConfig_t cfg;

    W25Q64_ReadData(
        FLASH_DEV_INDEX,
        (uint8_t *)&cfg,
        FLASH_ADDR_SYS_CONFIG,
        sizeof(cfg)
    );

    if (cfg.magic != MAGIC_SYS_CONFIG) {
        return false;
    }

    if (weight_anchor != NULL) {
        *weight_anchor = cfg.weight_anchor_g;
    }

    if (bsec_state != NULL &&
        bsec_len != NULL &&
        cfg.bsec_state_len <= sizeof(cfg.bsec_state)) {
        *bsec_len = cfg.bsec_state_len;
        memcpy(bsec_state, cfg.bsec_state, cfg.bsec_state_len);
    }

    return true;
}

void FlashMgr_SaveEnoseClasses(const ENose_t *e)
{
FlashRefConfig_t cfg;
cfg.magic = MAGIC_AI_REF;
memcpy(cfg.classes, e->cls, sizeof(e->cls));

W25Q64_EraseSector(FLASH_DEV_INDEX, FLASH_ADDR_AI_REF / 4096);
Flash_WriteBuffer(FLASH_ADDR_AI_REF, (uint8_t*)&cfg, sizeof(FlashRefConfig_t));


}

void FlashMgr_LoadEnoseClasses(ENose_t *e)
{
    FlashRefConfig_t cfg;

    if (e == NULL) {
        return;
    }

    W25Q64_ReadData(
        FLASH_DEV_INDEX,
        (uint8_t *)&cfg,
        FLASH_ADDR_AI_REF,
        sizeof(cfg)
    );

    if (cfg.magic == MAGIC_AI_REF) {
        memcpy(e->cls, cfg.classes, sizeof(e->cls));
    }
}

bool FlashMgr_DeleteReference(uint8_t ref_id, ENose_t *e)
{
if (ref_id >= ENOSE_NUM_CLASS) return false;
e->cls[ref_id].valid = 0;
e->cls[ref_id].n_samples = 0;
FlashMgr_SaveEnoseClasses(e);
return true;
}

bool FlashMgr_ModifyReferenceLabel(uint8_t ref_id, uint8_t new_label, ENose_t *e)
{
if (ref_id >= ENOSE_NUM_CLASS) return false;
e->cls[ref_id].mapped_label = new_label;
FlashMgr_SaveEnoseClasses(e);
return true;
}

void FlashMgr_AppendLog(const ENose_t *e, float temp, float hum, float risk, float loss)
{
FlashLogEntry_t entry = {0};
entry.timestamp     = SysTime_GetLocalTimestamp();
memcpy(entry.resp, e->resp, sizeof(e->resp));
entry.env_temp      = temp;
entry.env_hum       = hum;
entry.spoilage_risk = risk;
entry.weight_loss_g = loss;
entry.ai_state      = (uint8_t)e->state;

FlashMgr_AppendLogEntry(&entry);
}

void FlashMgr_AppendLogEntry(const FlashLogEntry_t *entry)
{
uint32_t idx;

if (entry == NULL) return;

idx = g_flash_log.head_index;

if (idx == 0) {
    W25Q64_EraseSector(FLASH_DEV_INDEX, FLASH_ADDR_LOG_BASE / 4096);
    if (g_flash_log.log_count == MAX_FLASH_LOGS) g_flash_log.log_count -= 64;
}
else if (idx == 64) {
    W25Q64_EraseSector(FLASH_DEV_INDEX, (FLASH_ADDR_LOG_BASE + 4096) / 4096);
    if (g_flash_log.log_count == MAX_FLASH_LOGS) g_flash_log.log_count -= 64;
}

W25Q64_WritePage(
    FLASH_DEV_INDEX,
    (uint8_t *)entry,
    FLASH_ADDR_LOG_BASE + idx * LOG_ENTRY_SIZE,
    LOG_ENTRY_SIZE
);

g_flash_log.head_index = (idx + 1) % MAX_FLASH_LOGS;
if (g_flash_log.log_count < MAX_FLASH_LOGS) {
    g_flash_log.log_count++;
}
}

bool FlashMgr_ReadLog(uint32_t offset_from_newest, FlashLogEntry_t *out_entry)
{
if (offset_from_newest >= g_flash_log.log_count) return false;

int32_t actual_idx = (g_flash_log.head_index - 1 - offset_from_newest);
if (actual_idx < 0) actual_idx += MAX_FLASH_LOGS;

W25Q64_ReadData(FLASH_DEV_INDEX, (uint8_t*)out_entry, FLASH_ADDR_LOG_BASE + actual_idx * LOG_ENTRY_SIZE, LOG_ENTRY_SIZE);
return (out_entry->timestamp != 0xFFFFFFFF);


}

void FlashMgr_ClearLogs(void)
{
W25Q64_EraseSector(FLASH_DEV_INDEX, FLASH_ADDR_LOG_BASE / 4096);
W25Q64_EraseSector(FLASH_DEV_INDEX, (FLASH_ADDR_LOG_BASE + 4096) / 4096);
g_flash_log.head_index = 0;
g_flash_log.log_count = 0;
}

bool FlashMgr_PromoteLogToRef(uint32_t log_offset, uint8_t target_ref_id, ENose_t *e)
{
FlashLogEntry_t entry;
if (!FlashMgr_ReadLog(log_offset, &entry)) return false;
if (target_ref_id >= ENOSE_NUM_CLASS) return false;

ENose_ClassRef_t *c = &e->cls[target_ref_id];
memcpy(c->centroid, entry.resp, sizeof(entry.resp));
c->n_samples = 1;
c->valid = 1;
c->mapped_label = entry.ai_state; 

FlashMgr_SaveEnoseClasses(e);
return true;


}

void FlashMgr_HistorySessionInit(void)
{
g_flash_history.head_index = 0U;
g_flash_history.record_count = 0U;
}

bool FlashMgr_AppendHistoryBatch(const FlashHistoryRecord_t *records, uint8_t count)
{
uint32_t index;
uint32_t write_addr;
uint32_t sector;
uint16_t bytes;

if (records == NULL || count == 0U || count > 4U) {
    return false;
}

index = g_flash_history.head_index;

/*
 * The worker submits four 32-byte records at a time. head_index therefore
 * remains 128-byte aligned and a batch never crosses a 256-byte page.
 */
if ((index % (4096U / FLASH_HISTORY_RECORD_SIZE)) == 0U) {
    sector = (FLASH_HISTORY_ADDR_BASE / 4096U) +
             (index / (4096U / FLASH_HISTORY_RECORD_SIZE));
    W25Q64_EraseSector(FLASH_HISTORY_DEV_INDEX, sector);
}

write_addr = FLASH_HISTORY_ADDR_BASE + index * FLASH_HISTORY_RECORD_SIZE;
bytes = (uint16_t)(count * FLASH_HISTORY_RECORD_SIZE);
W25Q64_WritePage(
    FLASH_HISTORY_DEV_INDEX,
    (uint8_t *)records,
    write_addr,
    bytes
);

g_flash_history.head_index = (index + count) % MAX_FLASH_HISTORY_RECORDS;
if (g_flash_history.record_count < MAX_FLASH_HISTORY_RECORDS) {
    uint32_t remaining = MAX_FLASH_HISTORY_RECORDS - g_flash_history.record_count;
    g_flash_history.record_count += (count < remaining) ? count : remaining;
}

return true;
}
