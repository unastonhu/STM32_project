#include "usb_reporter.h"
#include "system_data.h"
#include "usbd_cdc_if.h"
#include "FreeRTOS.h"
#include "cmsis_os.h"

#include "task.h"
#include <stdio.h>


// 🌟 架构师级防御：1024 字节巨型静态缓冲，彻底脱离 FreeRTOS 任务栈
static char usb_tx_buf[1024]; 

// 时间戳记录
static TickType_t last_slow_tick = 0;
static TickType_t last_wait_tick = 0;

void USB_Reporter_Init(void)
{
    sysData.slow_interval_ms = 30000; // 默认半分钟慢跑档位
    last_slow_tick = xTaskGetTickCount();
    last_wait_tick = xTaskGetTickCount();
}

void USB_Reporter_Routine(void)
{
    uint16_t tx_len;
    TickType_t current_tick = xTaskGetTickCount();

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
        
        // ⚡ 【发送快信号】(每次执行都发，跟进外层任务的 50ms 频率)
        tx_len = snprintf(usb_tx_buf, sizeof(usb_tx_buf), 
            "{\"cmd\":\"FAST\",\"dist\":%.1f,\"ir1\":%d,\"ir2\":%d,\"oz_lock\":%d}\r\n", 
            sysData.ui.distance, sysData.ir.ir1_blocked, sysData.ir.ir2_blocked, sysData.ozone_is_locked);
        
        // 关键防御：发送快信号，并等待底层 USB 接口释放（通常只需几微秒）
        while(CDC_Transmit_FS((uint8_t*)usb_tx_buf, tx_len) == USBD_BUSY) { osDelay(1); }

        // 🐢 【发送慢信号】(按档位时间发送)
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
                sysData.relays.ozone, sysData.relays.uv_lamp,
                sysData.relays.cool_fans[0], sysData.relays.cool_fans[1],
                sysData.relays.duct_fans[0], sysData.relays.duct_fans[1],
                sysData.relays.coolers[0], sysData.relays.coolers[1], sysData.relays.coolers[2], sysData.relays.coolers[3]
            );
            
            // 关键防御：只有快信号缓冲区彻底发完，才会把慢信号挤进发送管道
            while(CDC_Transmit_FS((uint8_t*)usb_tx_buf, tx_len) == USBD_BUSY) { osDelay(1); }
            
            last_slow_tick = current_tick; // 重置秒表
        }
    }
}
