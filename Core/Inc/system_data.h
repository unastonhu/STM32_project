#ifndef __SYSTEM_DATA_H
#define __SYSTEM_DATA_H

#include "main.h"

// ==========================================
// 1. 各大子系统专属数据卡片
// ==========================================
typedef struct {
    float   weight;
    int8_t  status;
} HX711_Data_t;

typedef struct {
    int8_t  temp;
    int8_t  hum;
    int8_t  status;
} DHT11_Data_t;

typedef struct {
    float   temp;
    int8_t  status;
} DS18B20_Data_t;

typedef struct {
    uint16_t raw;
    int32_t  voc_index;
    int8_t   status;
} SGP40_Data_t;

typedef struct {
    float    aht_temp;
    float    aht_hum;
    uint16_t ens_tvoc;
    uint16_t ens_eco2;
    uint8_t  ens_aqi;
    uint32_t ens_warmup_sec;
    int8_t   status;  // 1:OK, -1:AHT掉线, -2:预热, -3:地址错
} ENV_Data_t;

typedef struct {
    float   temp;
    float   hum;
    float   press;
    float   gas_res;
    int8_t  status;
} BME688_Data_t;

typedef struct {
    uint32_t id;
    uint32_t total_size_kb; 
    uint32_t used_size_kb;  
    int8_t   status;        
} W25Q64_Data_t;

typedef struct {
    float    distance;
    uint8_t  screen_awake;
    uint16_t screen_timeout;
} UI_Control_t;

typedef struct {
    uint8_t  ir1_blocked; // 模块 1 状态 (1:被遮挡, 0:未遮挡)
    uint8_t  ir2_blocked; // 模块 2 状态
    int8_t   status;      // 状态码
} IR_Sensor_t;

// ==========================================
// 2. 终极系统大盘 (SystemData_t)
// ==========================================
typedef struct {
    HX711_Data_t   hx711;
    DHT11_Data_t   dht11;
    DS18B20_Data_t ds18b20;
    SGP40_Data_t   sgp40;
    ENV_Data_t     env;
    BME688_Data_t  bme688;
    W25Q64_Data_t  flash1;
    W25Q64_Data_t  flash2;
    UI_Control_t   ui;
    IR_Sensor_t    ir;
} SystemData_t;

// 🌟 对外暴露全局数据变量（极其重要，别漏了 extern）
extern SystemData_t sysData;

// 对外暴露 UI 渲染函数接口
void System_PrintStatus(SystemData_t *sys);

#endif /* __SYSTEM_DATA_H */
