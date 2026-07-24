#include "usb_reporter.h"
#include "system_data.h"
#include "usbd_cdc_if.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "control.h"
#include "sys_time.h"

#include "flash_manager.h"
#include "data_export.h"
#include "dynamic_commands.h"

static char usb_tx_buf[1024];
static char usb_response_buf[384];
static uint16_t usb_response_len;
static bool usb_response_pending;
static DataExportChunk_t usb_export_chunk;
static bool usb_export_chunk_pending;

char usb_rx_buf[256];
volatile uint8_t usb_rx_ready = 0;

static TickType_t last_slow_tick = 0;
static TickType_t last_wait_tick = 0;

extern osMutexId_t flash_mutex;

bool USB_Reporter_QueueResponse(const char *format, ...)
{
va_list args;
int length;

if (usb_response_pending) {
    return false;
}

va_start(args, format);
length = vsnprintf(
    usb_response_buf,
    sizeof(usb_response_buf),
    format,
    args
);
va_end(args);

if (length <= 0 || (size_t)length >= sizeof(usb_response_buf)) {
    return false;
}

usb_response_len = (uint16_t)length;
usb_response_pending = true;
return true;
}

static bool USB_TransmitWithRetry(
    uint8_t *data,
    uint16_t length,
    uint8_t retry_limit)
{
uint8_t retries = 0U;
uint8_t result;

do {
    result = CDC_Transmit_FS(data, length);
    if (result != USBD_BUSY) {
        return result == USBD_OK;
    }
    osDelay(1);
} while (++retries <= retry_limit);

return false;
}

void USB_Reporter_Init(void)
{
sysData.slow_interval_ms = 10000;
last_slow_tick = HAL_GetTick();
last_wait_tick = HAL_GetTick();
usb_response_pending = false;
usb_export_chunk_pending = false;
}

void USB_Reporter_Routine(void)
{
uint16_t tx_len;
TickType_t current_tick = HAL_GetTick();

if (sysData.esp32_ready == 0) {
    if ((current_tick - last_wait_tick) >= 2000) {
        tx_len = snprintf(usb_tx_buf, sizeof(usb_tx_buf), 
                         "{\"status\": \"WAITING\", \"token\": \"S3_LINK_OK\"}\r\n");
        CDC_Transmit_FS((uint8_t*)usb_tx_buf, tx_len);
        last_wait_tick = current_tick;
    }
} 
else {
    tx_len = snprintf(usb_tx_buf, sizeof(usb_tx_buf), 
        "{\"cmd\":\"FAST\",\"dist\":%.1f,\"ir1\":%d,\"ir2\":%d,\"oz_lock\":%d}\r\n", 
        sysData.ui.distance, sysData.ir.ir1_blocked, sysData.ir.ir2_blocked, sysData.ozone_is_locked);
    
    uint8_t retry1 = 0;
    while(CDC_Transmit_FS((uint8_t*)usb_tx_buf, tx_len) == USBD_BUSY) { 
        osDelay(1); 
        if(++retry1 > 10) break; 
    }

    if ((current_tick - last_slow_tick) * portTICK_PERIOD_MS >= sysData.slow_interval_ms) {
        
        tx_len = snprintf(usb_tx_buf, sizeof(usb_tx_buf), 
            "{\"cmd\":\"SLOW\","
            "\"ts\":%lu,"
            "\"hx\":{\"w\":%.1f,\"s\":%d},"
            "\"dht\":{\"t\":%d,\"h\":%d,\"s\":%d},"
            "\"ds\":{\"t\":%.2f,\"s\":%d},"
            "\"sgp\":{\"v\":%ld,\"r\":%u,\"s\":%d},"
            "\"env\":{\"at\":%.1f,\"ah\":%.1f,\"tv\":%u,\"co\":%u,\"aq\":%d,\"s\":%d},"
            "\"bme\":{\"t\":%.1f,\"h\":%.1f,\"p\":%.1f,\"g\":%.0f,\"s\":%d,\"iaq\":%.1f,\"eco2\":%.0f,\"risk\":%.3f},"
            "\"mem\":{\"log_cnt\":%lu,\"sd\":%d},"
            "\"enose\":{\"mode\":%d,\"state\":%d}," 
            "\"k230\":{\"ap\":%d,\"bn\":%d,\"or\":%d},"
            "\"relays\":{\"oz\":%d,\"uv\":%d,\"cf\":[%d,%d],\"df\":[%d,%d],\"tec\":[%d,%d,%d,%d]}"
            "}\r\n", 
            
            (unsigned long)SysTime_GetLocalTimestamp(),
            sysData.hx711.weight, sysData.hx711.status,
            sysData.dht11.temp, sysData.dht11.hum, sysData.dht11.status,
            sysData.ds18b20.temp, sysData.ds18b20.status,
            (long)sysData.sgp40.voc_index, sysData.sgp40.raw, sysData.sgp40.status,
            sysData.env.aht_temp, sysData.env.aht_hum, sysData.env.ens_tvoc, sysData.env.ens_eco2, sysData.env.ens_aqi, sysData.env.status,
            sysData.bme688.temp, sysData.bme688.hum, sysData.bme688.press, sysData.bme688.gas_res, sysData.bme688.status,sysData.bme688.iaq_index,sysData.bme688.eco2,sysData.bme688.food_spoilage_risk,
            
            g_flash_log.log_count, sysData.sdcard.status,
            
            (int)sysData.enose.mode, (int)sysData.enose.state,
            sysData.k230.apple, sysData.k230.banana, sysData.k230.orange, 
            sysData.relays.ozone, sysData.relays.uv_lamp,
            sysData.relays.cool_fans[0], sysData.relays.cool_fans[1],
            sysData.relays.duct_fans[0], sysData.relays.duct_fans[1],
            sysData.relays.coolers[0], sysData.relays.coolers[1], sysData.relays.coolers[2], sysData.relays.coolers[3]
        );
        
        uint8_t retry2 = 0;
        while(CDC_Transmit_FS((uint8_t*)usb_tx_buf, tx_len) == USBD_BUSY) { 
            osDelay(1); 
            if(++retry2 > 20) break;
        }
        
        last_slow_tick = current_tick; 
    }
}

/*
 * FAST/SLOW 始终先发送，导出数据只能使用它们之后的空闲带宽。
 * 若端点仍忙，响应或导出块会保留到下一轮，不覆盖也不丢弃。
 */
if (usb_response_pending) {
    if (USB_TransmitWithRetry(
            (uint8_t *)usb_response_buf,
            usb_response_len,
            20U)) {
        usb_response_pending = false;
    }
}
else {
    if (!usb_export_chunk_pending) {
        usb_export_chunk_pending =
            DataExport_TryGetChunk(&usb_export_chunk);
    }
    if (usb_export_chunk_pending &&
        USB_TransmitWithRetry(
            (uint8_t *)usb_export_chunk.data,
            usb_export_chunk.length,
            20U)) {
        usb_export_chunk_pending = false;
    }
}

}

void USB_Command_Parser(char *json_str)
{
char *ptr;
int num = 0, num2 = 0;

if (strstr(json_str, "\"cmd\":\"START\"")) sysData.esp32_ready = 1; 
else if (strstr(json_str, "\"cmd\":\"MODE_5MIN\"")) sysData.slow_interval_ms = 300000; 
else if (strstr(json_str, "\"cmd\":\"MODE_30SEC\"")) sysData.slow_interval_ms = 30000; 

// 1. 时间注入
if (DynamicCommands_Handle(json_str)) {
    return;
}

if (strstr(json_str, "TIME:")) SysTime_ParseUSBCommand(json_str, strlen(json_str));

// 2. 边缘数据库高级管控
if (strstr(json_str, "\"cmd\":\"CLEAR_LOGS\"")) {
    osMutexAcquire(flash_mutex, osWaitForever);
    FlashMgr_ClearLogs();
    osMutexRelease(flash_mutex);
}
else if (strstr(json_str, "\"cmd\":\"GET_LOG\"")) {
    if ((ptr = strstr(json_str, "\"offset\":")) != NULL && sscanf(ptr, "\"offset\":%d", &num) == 1) {
        FlashLogEntry_t log;
        osMutexAcquire(flash_mutex, osWaitForever);
        bool log_found = FlashMgr_ReadLog((uint32_t)num, &log);
        osMutexRelease(flash_mutex);
        if(log_found) {
            int len = snprintf(usb_tx_buf, sizeof(usb_tx_buf),
                "{\"cmd\":\"LOG_DATA\",\"offset\":%d,\"ts\":%lu,\"t\":%.1f,\"h\":%.1f,\"risk\":%.3f,\"loss\":%.1f,\"ai\":%d}\r\n",
                num, log.timestamp, log.env_temp, log.env_hum, log.spoilage_risk, log.weight_loss_g, log.ai_state);
            CDC_Transmit_FS((uint8_t*)usb_tx_buf, len);
        }
    }
}
else if (strstr(json_str, "\"cmd\":\"SET_REF\"")) {
    ptr = strstr(json_str, "\"offset\":");
    char *ptr2 = strstr(json_str, "\"id\":");
    if (ptr && ptr2 && sscanf(ptr, "\"offset\":%d", &num) == 1 && sscanf(ptr2, "\"id\":%d", &num2) == 1) {
        osMutexAcquire(flash_mutex, osWaitForever);
        FlashMgr_PromoteLogToRef((uint32_t)num, (uint8_t)num2, &sysData.enose);
        osMutexRelease(flash_mutex);
    }
}
else if (strstr(json_str, "\"cmd\":\"GET_REF\"")) {
    if ((ptr = strstr(json_str, "\"id\":")) != NULL && sscanf(ptr, "\"id\":%d", &num) == 1) {
        if (num >= 0 && num < ENOSE_NUM_CLASS) {
            ENose_ClassRef_t *c = &sysData.enose.cls[num];
            int len = snprintf(usb_tx_buf, sizeof(usb_tx_buf),
                "{\"cmd\":\"REF_DATA\",\"id\":%d,\"v\":%d,\"label\":%d,\"resp\":[%.3f,%.3f,%.3f,%.3f,%.3f]}\r\n",
                num, c->valid, c->mapped_label, c->centroid[0], c->centroid[1], c->centroid[2], c->centroid[3], c->centroid[4]);
            CDC_Transmit_FS((uint8_t*)usb_tx_buf, len);
        }
    }
}
else if (strstr(json_str, "\"cmd\":\"DEL_REF\"")) {
    if ((ptr = strstr(json_str, "\"id\":")) != NULL && sscanf(ptr, "\"id\":%d", &num) == 1) {
        osMutexAcquire(flash_mutex, osWaitForever);
        FlashMgr_DeleteReference((uint8_t)num, &sysData.enose);
        osMutexRelease(flash_mutex);
    }
}
else if (strstr(json_str, "\"cmd\":\"SAVE_SYS\"")) {
    extern FridgeWeightEngine_t g_weight_engine;
    osMutexAcquire(flash_mutex, osWaitForever);
    FlashMgr_SaveSysState(NULL, 0, g_weight_engine.base_anchor_weight);
    osMutexRelease(flash_mutex);
}

// 3. 执行器控制与示教
if (strstr(json_str, "\"oz\":1")) {
    if (sysData.ozone_is_locked == 0) sysData.relays.ozone = 1;
} else if (strstr(json_str, "\"oz\":0")) sysData.relays.ozone = 0;
if (strstr(json_str, "\"uv\":1")) sysData.relays.uv_lamp = 1;
else if (strstr(json_str, "\"uv\":0")) sysData.relays.uv_lamp = 0;

if ((ptr = strstr(json_str, "\"tec\":")) != NULL) {
    if (sscanf(ptr, "\"tec\":%d", &num) == 1) Control_Set_Coolers((uint8_t)num);
}
if ((ptr = strstr(json_str, "\"c_fan\":")) != NULL) {
    if (sscanf(ptr, "\"c_fan\":%d", &num) == 1) Control_Set_CoolFans((uint8_t)num);
}
if ((ptr = strstr(json_str, "\"d_fan\":")) != NULL) {
    if (sscanf(ptr, "\"d_fan\":%d", &num) == 1) Control_Set_DuctFans((uint8_t)num);
}
if ((ptr = strstr(json_str, "\"teach\":")) != NULL) {
    if (sscanf(ptr, "\"teach\":%d", &num) == 1) {
        if (num >= 0 && num < ENOSE_NUM_CLASS) {
            uint32_t now = HAL_GetTick();
            ENose_SetMode(&sysData.enose, MODE_LEARN, now);
            ENose_TeachCurrent(&sysData.enose, (ENose_State_t)num);
            sysData.enose.cls[num].mapped_label = (uint8_t)num;
            ENose_SetMode(&sysData.enose, MODE_RUN, now);
            osMutexAcquire(flash_mutex, osWaitForever);
            FlashMgr_SaveEnoseClasses(&sysData.enose);
            osMutexRelease(flash_mutex);
        }
    }
}


}
