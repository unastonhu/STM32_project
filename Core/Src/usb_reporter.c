#include "usb_reporter.h"
#include "system_data.h"
#include "usbd_cdc_if.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

#include "task.h"
#include <stdio.h>
#include <string.h>
#include "system_data.h"

#include "control.h"


// 架构师级防御：1024 字节巨型静态缓冲，彻底脱离 FreeRTOS 任务栈
static char usb_tx_buf[1024]; 

char usb_rx_buf[256];
volatile uint8_t usb_rx_ready = 0;

// 时间戳记录
static TickType_t last_slow_tick = 0;
static TickType_t last_wait_tick = 0;

void USB_Reporter_Init(void)
{
    sysData.slow_interval_ms = 10000; // 默认半分钟慢跑档位
    last_slow_tick = HAL_GetTick();
    last_wait_tick = HAL_GetTick();
}

void USB_Reporter_Routine(void)
{
    uint16_t tx_len;
    TickType_t current_tick = HAL_GetTick();

    if (sysData.esp32_ready == 0) {
        // --- 1. 未握手状态：每 2 秒发一次心跳，绝不堵死 CPU ---
        if ((current_tick - last_wait_tick) >= 2000) {
            tx_len = snprintf(usb_tx_buf, sizeof(usb_tx_buf), 
                             "{\"status\": \"WAITING\", \"token\": \"S3_LINK_OK\"}\r\n");
            CDC_Transmit_FS((uint8_t*)usb_tx_buf, tx_len);
            last_wait_tick = current_tick;
        }
    } 
    else {
        // --- 2. 握手成功：数据狂飙状态 ---
        
        // 【发送快信号】(每次执行都发，跟进外层任务的 50ms 频率)
        tx_len = snprintf(usb_tx_buf, sizeof(usb_tx_buf), 
            "{\"cmd\":\"FAST\",\"dist\":%.1f,\"ir1\":%d,\"ir2\":%d,\"oz_lock\":%d}\r\n", 
            sysData.ui.distance, sysData.ir.ir1_blocked, sysData.ir.ir2_blocked, sysData.ozone_is_locked);
        
        // 关键防御：发送快信号，并等待底层 USB 接口释放（通常只需几微秒）
        // 修复：最多等 10ms，发不出去就算了，扔掉这包数据，保命要紧！
        uint8_t retry1 = 0;
        while(CDC_Transmit_FS((uint8_t*)usb_tx_buf, tx_len) == USBD_BUSY) { 
            osDelay(1); 
            if(++retry1 > 10) break; // 超时强行逃生
        }


        // 【发送慢信号】(按档位时间发送)
       if ((current_tick - last_slow_tick) * portTICK_PERIOD_MS >= sysData.slow_interval_ms) {
            
    tx_len = snprintf(usb_tx_buf, sizeof(usb_tx_buf), 
        "{\"cmd\":\"SLOW\","
        "\"hx\":{\"w\":%.1f,\"s\":%d},"
        "\"dht\":{\"t\":%d,\"h\":%d,\"s\":%d},"
        "\"ds\":{\"t\":%.2f,\"s\":%d},"
        "\"sgp\":{\"v\":%ld,\"r\":%u,\"s\":%d},"
        "\"env\":{\"at\":%.1f,\"ah\":%.1f,\"tv\":%u,\"co\":%u,\"aq\":%d,\"s\":%d},"
        "\"bme\":{\"t\":%.1f,\"h\":%.1f,\"p\":%.1f,\"g\":%.0f,\"s\":%d},"
        "\"mem\":{\"f1\":%d,\"f2\":%d,\"sd\":%d},"
        "\"enose\":{\"mode\":%d,\"state\":%d}," 
        "\"k230\":{\"ap\":%d,\"bn\":%d,\"or\":%d}," //  1. 在这里加上 K230 的 JSON 占位符
        "\"relays\":{\"oz\":%d,\"uv\":%d,\"cf\":[%d,%d],\"df\":[%d,%d],\"tec\":[%d,%d,%d,%d]}"
        "}\r\n", 
        
        sysData.hx711.weight, sysData.hx711.status,
        sysData.dht11.temp, sysData.dht11.hum, sysData.dht11.status,
        sysData.ds18b20.temp, sysData.ds18b20.status,
        (long)sysData.sgp40.voc_index, sysData.sgp40.raw, sysData.sgp40.status,
        sysData.env.aht_temp, sysData.env.aht_hum, sysData.env.ens_tvoc, sysData.env.ens_eco2, sysData.env.ens_aqi, sysData.env.status,
        sysData.bme688.temp, sysData.bme688.hum, sysData.bme688.press, sysData.bme688.gas_res, sysData.bme688.status,
        sysData.flash1.rw_test, sysData.flash2.rw_test, sysData.sdcard.status,
        (int)sysData.enose.mode, (int)sysData.enose.state,
        
        sysData.k230.apple, sysData.k230.banana, sysData.k230.orange, //  2. 在这里把全局大盘的水果变量灌进去

        sysData.relays.ozone, sysData.relays.uv_lamp,
        sysData.relays.cool_fans[0], sysData.relays.cool_fans[1],
        sysData.relays.duct_fans[0], sysData.relays.duct_fans[1],
        sysData.relays.coolers[0], sysData.relays.coolers[1], sysData.relays.coolers[2], sysData.relays.coolers[3]
    );
    
    // 修复：同样加上超时机制
        uint8_t retry2 = 0;
        while(CDC_Transmit_FS((uint8_t*)usb_tx_buf, tx_len) == USBD_BUSY) { 
            osDelay(1); 
            if(++retry2 > 20) break; // 慢信号比较大，多等一会儿，最多20ms
        }
    
    last_slow_tick = current_tick; 
}
}  }
            
// ==========================================================
// 专门的 USB JSON 解析执行函数 (在 RTOS 任务中安全调用)
// ==========================================================
void USB_Command_Parser(char *json_str)
{
    char *ptr;
    int num = 0;

    // 1. 系统握手与档位控制
    if (strstr(json_str, "\"cmd\":\"START\"")) {
        sysData.esp32_ready = 1; 
    }
    else if (strstr(json_str, "\"cmd\":\"MODE_5MIN\"")) {
        sysData.slow_interval_ms = 300000; 
    }
    else if (strstr(json_str, "\"cmd\":\"MODE_30SEC\"")) {
        sysData.slow_interval_ms = 30000; 
    }

    // 2. 独立安全设备 (臭氧、紫外线)
    if (strstr(json_str, "\"oz\":1")) {
        if (sysData.ozone_is_locked == 0) sysData.relays.ozone = 1;
    }
    else if (strstr(json_str, "\"oz\":0")) {
        sysData.relays.ozone = 0;
    }
    if (strstr(json_str, "\"uv\":1")) sysData.relays.uv_lamp = 1;
    else if (strstr(json_str, "\"uv\":0")) sysData.relays.uv_lamp = 0;

    // 3. 高级群组分配
    if ((ptr = strstr(json_str, "\"tec\":")) != NULL) {
        if (sscanf(ptr, "\"tec\":%d", &num) == 1) Control_Set_Coolers((uint8_t)num);
    }
    if ((ptr = strstr(json_str, "\"c_fan\":")) != NULL) {
        if (sscanf(ptr, "\"c_fan\":%d", &num) == 1) Control_Set_CoolFans((uint8_t)num);
    }
    if ((ptr = strstr(json_str, "\"d_fan\":")) != NULL) {
        if (sscanf(ptr, "\"d_fan\":%d", &num) == 1) Control_Set_DuctFans((uint8_t)num);
    }

    // 4. AI 电子鼻“一键示教”
    if ((ptr = strstr(json_str, "\"teach\":")) != NULL) {
        if (sscanf(ptr, "\"teach\":%d", &num) == 1) {
            uint32_t now = xTaskGetTickCount(); // 既然封装后是在任务里跑，用这个绝对安全
            ENose_SetMode(&sysData.enose, MODE_LEARN, now);
            ENose_TeachCurrent(&sysData.enose, (ENose_State_t)num);
            ENose_SetMode(&sysData.enose, MODE_RUN, now);
        }
    }
}
