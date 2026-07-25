#ifndef __SYSTEM_DATA_H
#define __SYSTEM_DATA_H

#include "main.h"
#include "enose.h"


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

    // [新增字段]: 给 AI 结果留位置
    float iaq_index;
    float eco2;
    float food_spoilage_risk; // 存那个 AI 异味/硫化物检测率
    int16_t algorithm_status;  // 最近一次 BSEC 返回码，0=正常，负数=错误
    uint8_t accuracy;          // BSEC IAQ 精度等级 0..3
    uint8_t reserved;

} BME688_Data_t;

typedef struct {
    uint32_t id;
    int8_t   rw_test;      //读写健康度 (1:完美通过, -1:损坏, 0:未测试)
    uint32_t total_size_kb; 
    uint32_t used_size_kb;  
    int8_t   status;        
} W25Q64_Data_t;

// SD卡 专属数据卡片
typedef struct {
    uint8_t  type;          // 卡类型 (如 SDHC, SDXC)
    uint32_t capacity_mb;   // 总容量 (MB)
    int8_t   status;        // 状态 (1:在线, 0:离线)
} SD_Card_Data_t;

typedef struct {
    float    distance;
    uint8_t  screen_awake;
    uint16_t screen_timeout;
} UI_Control_t;

typedef struct {
    uint8_t  ir1_blocked; // 模块 1 状态 (1:被遮挡, 0:未遮挡)
    uint8_t  ir2_blocked; // 模块 2 状态
    int8_t   status;      // 状态码,无遮挡视作开门，status = 0

} IR_Sensor_t;

// ==========================================
// 2. 继电器执行器阵列 (6路小电流继电器 + 4路大功率制冷)
// ==========================================
typedef struct {
    uint8_t  ozone;         // CH1: 臭氧发生器 (0关/1开)
    uint8_t  uv_lamp;       // CH2: 紫外线杀菌灯 (0关/1开)
    
    // 4 个风扇两两并联，只占 2 个通道
    uint8_t  cool_fans[2];  // CH3-CH4: 散热大风扇组 (0关/1开，每组带2个)
    uint8_t  duct_fans[2];  // CH5-CH6: 反应区搅动小风扇组 (0关/1开，每组带2个)
    
    // 4 路独立大功率制冷通道
    uint8_t  coolers[4];    // CH7-CH10: 制冷模块 TEC (0关/1开)
} Relay_Status_t;


// ==========================================
// 新增：K230 视觉协处理器专属数据卡片
// ==========================================
typedef struct {
    uint16_t apple;
    uint16_t banana;
    uint16_t orange;
    int8_t   status;  // 1:在线, 0:离线
} K230_Vision_t;


// ==========================================
// 3. 终极系统大盘 (SystemData_t)
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
    SD_Card_Data_t sdcard;

    UI_Control_t   ui;
    IR_Sensor_t    ir;

    //AI 电子鼻状态机
    ENose_t        enose;

    // 补全：将继电器状态卡片收编进系统大盘
    Relay_Status_t relays;
    
    uint32_t last_esp32_heartbeat;

    K230_Vision_t  k230;

    // 新增：臭氧高危设备安全锁 (时间戳全用 FreeRTOS 的 Tick)
    uint32_t ozone_start_tick;  // 记录臭氧开启的时刻
    uint32_t ozone_lock_tick;   // 记录臭氧进入死锁的时刻
    uint8_t  ozone_is_locked;   // 核心锁：1=死锁中(绝对无法开启)，0=正常
    
    //  补全：新增上位机就绪标志与慢数据档位控制
    uint8_t  esp32_ready;       // 0=未握手等待中，1=握手成功开始发业务数据
    uint32_t slow_interval_ms;  // 慢数据上报间隔时间 (ms)
} SystemData_t;


// 对外暴露全局数据变量
extern SystemData_t sysData;

// 对外暴露 UI 渲染函数接口
void System_PrintStatus(SystemData_t *sys);

#endif /* __SYSTEM_DATA_H */
