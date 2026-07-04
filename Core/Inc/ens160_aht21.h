#ifndef __ENS160_AHT21_H
#define __ENS160_AHT21_H

#include "main.h"

// ==========================================
// 二合一环境传感器模块 (ENS160 + AHT21)
// ==========================================

// 初始化传感器并启动引擎
void ENV_Module_Init(I2C_HandleTypeDef *hi2c);

// 流水线全自动读取：AHT21采集 -> 补偿给ENS160 -> ENS160解算
// 返回值：1=成功, -1=AHT21掉线, -2=ENS160掉线或数据未就绪
int8_t ENV_Module_ReadAll(float *temp, float *hum, uint16_t *tvoc, uint16_t *eco2, uint8_t *aqi);



#endif /* __ENS160_AHT21_H */
