#include "sgp40.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"  
#include "sensirion_gas_index_algorithm.h" // 🌟 引入官方核心算法库

#define SGP40_I2C_ADDR (0x59 << 1)

static I2C_HandleTypeDef *sgp40_i2c = NULL;
static GasIndexAlgorithmParams voc_engine; // 🌟 隐藏在内部的算法引擎

// CRC 校验算法 (不变)
static uint8_t SGP40_CalcCrc(uint8_t data[2]) {
    uint8_t crc = 0xFF;
    for(int i = 0; i < 2; i++) {
        crc ^= data[i];
        for(uint8_t bit = 8; bit > 0; --bit) {
            if(crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

void SGP40_Init(I2C_HandleTypeDef *hi2c) {
    sgp40_i2c = hi2c;
    osDelay(10); // 上电缓冲



    // 🌟 在初始化硬件的同时，把官方算法引擎也给启动了
    GasIndexAlgorithm_init(&voc_engine, GasIndexAlgorithm_ALGORITHM_TYPE_VOC);
    GasIndexAlgorithm_set_tuning_parameters(&voc_engine, 100, 720, 720, 180, 50, 230);
}

// 内部函数：向芯片发送带“温湿度补偿”的测量指令
static int8_t SGP40_MeasureRawCompensated(float hum, float temp, uint16_t *sraw_voc) {
    uint8_t cmd[8];
    cmd[0] = 0x26;
    cmd[1] = 0x0F; // 测量指令

    // 范围保护，防止算爆
    if(hum < 0.0f) hum = 0.0f;
    if(hum > 100.0f) hum = 100.0f;
    if(temp < -45.0f) temp = -45.0f;
    if(temp > 130.0f) temp = 130.0f;

    // 🌟 严格遵循官方手册的公式，将人类温湿度换算为微型加热丝需要的 tick 值
    uint16_t rh_ticks = (uint16_t)((hum * 65535.0f) / 100.0f);
    uint16_t t_ticks  = (uint16_t)(((temp + 45.0f) * 65535.0f) / 175.0f);

    // 湿度数据 + 湿度CRC
    cmd[2] = (rh_ticks >> 8) & 0xFF;
    cmd[3] = rh_ticks & 0xFF;
    cmd[4] = SGP40_CalcCrc(&cmd[2]);

    // 温度数据 + 温度CRC
    cmd[5] = (t_ticks >> 8) & 0xFF;
    cmd[6] = t_ticks & 0xFF;
    cmd[7] = SGP40_CalcCrc(&cmd[5]);

    // 1. 发送带补偿参数的指令
    if (HAL_I2C_Master_Transmit(sgp40_i2c, SGP40_I2C_ADDR, cmd, 8, 1000) != HAL_OK) return -1;
    
    // 2. 加热丝反应时间
    osDelay(30); 

    // 3. 读取底层数据
    uint8_t rx_buf[3] = {0};
    if (HAL_I2C_Master_Receive(sgp40_i2c, SGP40_I2C_ADDR, rx_buf, 3, 1000) != HAL_OK) return -1;
    if (SGP40_CalcCrc(rx_buf) != rx_buf[2]) return -2;

    *sraw_voc = (rx_buf[0] << 8) | rx_buf[1];
    return 1;
}

// 🌟 对外暴露的终极接口
int8_t SGP40_GetVOCIndex(float hum, float temp, uint16_t *raw_signal, int32_t *voc_index) {
    // 1. 获取底层带温湿度补偿的 Raw 数据
    int8_t status = SGP40_MeasureRawCompensated(hum, temp, raw_signal);
    
    // 2. 如果硬件没掉线，直接扔进官方算法解算指数
    if (status == 1) {
        GasIndexAlgorithm_process(&voc_engine, (int32_t)(*raw_signal), voc_index);
    } else {
        // 如果断线了，强行归零报警
        *raw_signal = 0;
        *voc_index = 0;
    }
    
    return status;
}
