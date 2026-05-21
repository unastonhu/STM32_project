#ifndef __MQ_SENSOR_H
#define __MQ_SENSOR_H

#include "stm32f4xx_hal.h"

// 为了方便记忆，给 4 个通道起个名字 (对应数组的 0,1,2,3)
#define MQ3_1_CH    0  // PA0 上的酒精传感器 1
#define MQ3_2_CH    1  // PA1 上的酒精传感器 2
#define MQ135_1_CH  2  // PA2 上的空气传感器 1
#define MQ135_2_CH  3  // PA3 上的空气传感器 2

// ---------------- 暴露给外部的接口函数 ----------------

// 初始化传感器 (启动ADC的DMA搬运)
void MQ_Init(void);

// 获取传感器的原始ADC值 (范围 0-4095)
uint16_t MQ_Get_RawValue(uint8_t channel);

// 获取传感器输出的实际电压值 (范围 0-3.3V)
float MQ_Get_Voltage(uint8_t channel);

float MQ3_Get_mgL(float voltage); // 根据电压计算酒精浓度 (单位 mg/L)
float MQ135_Get_PPM(float voltage); // 根据电压计算空气质量指数 (单位 PPM)

#endif
