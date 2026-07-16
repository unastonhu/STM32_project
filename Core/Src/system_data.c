#include "system_data.h"
#include <stdio.h> // 需要用到 printf
#include "FreeRTOS.h"
#include "task.h"


// 这里是全系统唯一一次实例化！真正的物理内存开辟在这里！
SystemData_t sysData = {0}; // {0} 保证开机时所有状态默认都是 0 (离线/初始状态/全关)

// 独立的 UI 渲染引擎函数
void System_PrintStatus(SystemData_t *sys) {
    printf("\r\n============================ SYSTEM STATUS ====================================\r\n");

    // 1. HX711
    if (sys->hx711.status == 1) printf("[   HX711   ] Weight   : %.1f g\r\n", sys->hx711.weight);
    else printf("[   HX711   ] Error    : Offline!\r\n");

    // 2. DHT11
    if (sys->dht11.status == 1) printf("[   DHT11   ] Temp     : %d C       | Hum : %d %%\r\n", sys->dht11.temp, sys->dht11.hum);
    else printf("[   DHT11   ] Error    : Code %d\r\n", sys->dht11.status);

    // 3. DS18B20
    if (sys->ds18b20.status == 1) printf("[ DS18B20 ] Temp     : %.2f C\r\n", sys->ds18b20.temp);
    else printf("[ DS18B20 ] Error    : Offline!\r\n");

    // 4. SGP40
    if (sys->sgp40.status == 1) printf("[   SGP40   ] RawVOC   : %u ticks | VOC Index : %ld\r\n", sys->sgp40.raw, (long)sys->sgp40.voc_index);
    else printf("[   SGP40   ] Error    : Code %d\r\n", sys->sgp40.status);

    // 5. AHT21 + ENS160
    if (sys->env.status != -1) {
        printf("[   AHT21   ] Temp     : %.2f C    | Hum : %.2f %%\r\n", sys->env.aht_temp, sys->env.aht_hum);
        if (sys->env.status == 1) 
            printf("[   ENS160  ] TVOC     : %u ppb     | eCO2: %u ppm   | AQI: %d\r\n", sys->env.ens_tvoc, sys->env.ens_eco2, sys->env.ens_aqi);
        else if (sys->env.status == -2) 
            printf("[   ENS160  ] Status   : Warming up... ( %lu sec elapsed )\r\n", (unsigned long)sys->env.ens_warmup_sec);
        else if (sys->env.status == -3) 
            printf("[   ENS160  ] Error    : I2C Address Wrong (Try 0x53)!\r\n");
    } else {
        printf("[ ENV_MOD  ] Error    : AHT21 Offline!\r\n");
    }

    // 6. BME688
    if (sys->bme688.status == 1) printf("[   BME688  ] Pressure : %.2f hPa | Gas Res   : %.0f Ohms\r\n", sys->bme688.press, sys->bme688.gas_res);
    else printf("[   BME688  ] Error    : Offline!\r\n");

    // 7. W25Q64_1
    if (sys->flash1.id == 0xEF4017) {
        printf("[ W25Q64_1 ] Status   : 8MB Flash Ready! (ID: 0x%lX)\r\n", sys->flash1.id);
        if (sys->flash1.rw_test == 1)      printf("[ W25Q64_1 ] R/W Test : PASSED (Magic Word Match!)\r\n");
        else if (sys->flash1.rw_test == -1) printf("[ W25Q64_1 ] R/W Test : FAILED (Storage Damaged!)\r\n");
    } else if (sys->flash1.id == 0 || sys->flash1.id == 0xFFFFFF) {
        printf("[ W25Q64_1 ] Error    : SPI Offline or Line Disconnected!\r\n");
    } else {
        printf("[ W25Q64_1 ] Warning  : Unknown ID (Read: 0x%lX)\r\n", sys->flash1.id);
    }

    // 8. W25Q64_2
    if (sys->flash2.id == 0xEF4017) {
        printf("[ W25Q64_2 ] Status   : 8MB Flash Ready! (ID: 0x%lX)\r\n", sys->flash2.id);
        if (sys->flash2.rw_test == 1)      printf("[ W25Q64_2 ] R/W Test : PASSED (Magic Word Match!)\r\n");
        else if (sys->flash2.rw_test == -1) printf("[ W25Q64_2 ] R/W Test : FAILED (Storage Damaged!)\r\n");
    } else if (sys->flash2.id == 0 || sys->flash2.id == 0xFFFFFF) {
        printf("[ W25Q64_2 ] Error    : SPI Offline or Line Disconnected!\r\n");
    } else {
        printf("[ W25Q64_2 ] Warning  : Unknown ID (Read: 0x%lX)\r\n", sys->flash2.id);
    }

    // 9. 红外对射模块阵列
    printf("[ IR_SENS  ] Sensor 1 : %s | Sensor 2 : %s\r\n", 
           sys->ir.ir1_blocked ? "BLOCKED" : "CLEAR", 
           sys->ir.ir2_blocked ? "BLOCKED" : "CLEAR");
    if (sys->ir.status == 1) printf("[ IR_SENS  ] Status   : Both Sensors Blocked!\r\n");
    else printf("[ IR_SENS  ] Status   : Sensors Clear!\r\n");

    //  10. 新增：14路执行器状态看板 (工业级美化打印)
    printf("---------------------------- ACTUATORS & LINK ---------------------------------\r\n");
    printf("[  PURIFY  ] Ozone Gas: %s          | UV Sterilizer: %s\r\n",
           sys->relays.ozone ? "ON " : "OFF", 
           sys->relays.uv_lamp ? "ON " : "OFF");
           
    printf("[   FANS   ] Cool Fan A(大): %s     | Cool Fan B(大): %s\r\n",
           sys->relays.cool_fans[0] ? "ON " : "OFF", 
           sys->relays.cool_fans[1] ? "ON " : "OFF");
           
    printf("[   FANS   ] Duct Fan A(小): %s     | Duct Fan B(小): %s\r\n",
           sys->relays.duct_fans[0] ? "ON " : "OFF", 
           sys->relays.duct_fans[1] ? "ON " : "OFF");
           
    printf("[ COOLERS  ] TEC 1: %s | TEC 2: %s | TEC 3: %s | TEC 4: %s\r\n",
           sys->relays.coolers[0] ? "ON " : "OFF", sys->relays.coolers[1] ? "ON " : "OFF",
           sys->relays.coolers[2] ? "ON " : "OFF", sys->relays.coolers[3] ? "ON " : "OFF");

    //  11. 新增：ESP32 通讯链路大盘状态
    printf("[ USB_LINK ] ESP32 Status: %s | Report Interval: %.1f sec\r\n",
           sys->esp32_ready ? "CONNECTED " : "DISCONNECTED ",
           (float)sys->slow_interval_ms / 1000.0f);

     printf("[ USB_LINK ] Ozone Lock : %s\r\n", sys->ozone_is_locked ? "LOCKED" : "UNLOCKED");
     
     printf("[ USB_LINK ] K230 Vision : %s | Apple: %d | Banana: %d | Orange: %d\r\n",
           sys->k230.status ? "ONLINE " : "OFFLINE ",
           sys->k230.apple, sys->k230.banana, sys->k230.orange);

     printf("[ USB_LINK ] Last ESP32 Heartbeat: %lu ticks ago\r\n", (unsigned long)(xTaskGetTickCount() - sys->last_esp32_heartbeat));

     printf("[ USB_LINK ] Ozone Lock Timestamp: %lu | Ozone Start Timestamp: %lu\r\n", 
           (unsigned long)sys->ozone_lock_tick, (unsigned long)sys->ozone_start_tick);
           
     printf("[ USB_LINK ] System Tick Count: %lu\r\n", (unsigned long)xTaskGetTickCount());
     
     



    printf("====================================================================================\r\n");
}


