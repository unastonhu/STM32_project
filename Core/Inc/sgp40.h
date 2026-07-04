#ifndef __SGP40_H
#define __SGP40_H

#include "main.h"

// 1. 初始化 SGP40 硬件，并启动内置的 VOC AI 算法引擎
void SGP40_Init(I2C_HandleTypeDef *hi2c);

// 2. 一键获取最终数据 (自带硬件级温湿度补偿 + 官方算法解析)
// 参数:
//   hum:        当前环境湿度 (0~100 %)
//   temp:       当前环境温度 (-45~130 C)
//   raw_signal: 传出内部的原始 ticks 数据 (供观察调试)
//   voc_index:  传出最终的 0~500 异味指数
// 返回值: 1=成功, -1=硬件掉线, -2=CRC干扰报错
int8_t SGP40_GetVOCIndex(float hum, float temp, uint16_t *raw_signal, int32_t *voc_index);

#endif /* __SGP40_H */
