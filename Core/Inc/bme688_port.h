/**
 * @file    bme688_port.h
 * @brief   BME688 传感器底层接口头文件
 *
 * [修改记录]
 * 1. 初始版本：包含 BME688 基础初始化与 Forced Mode 强制读取接口声明。
 */

#ifndef __BME688_PORT_H
#define __BME688_PORT_H

#include <stdbool.h>

#include "main.h"

// 1. 初始化 BME688 传感器
int8_t BME688_Port_Init(I2C_HandleTypeDef *hi2c);

// BSEC 任务必须等底层设备初始化完成后才能访问 bme_dev。
bool BME688_Port_IsReady(void);

// 2. 触发并读取数据 (Forced Mode: 测一次就休眠，不占总线)
// 返回值：1 = 成功，-1 = 通信失败或数据未就绪
int8_t BME688_Port_Read(float *temp, float *hum, float *press, float *gas_res);

#endif /* __BME688_PORT_H */
