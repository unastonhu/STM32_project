#ifndef __HX711_H
#define __HX711_H

#include "main.h"

// 核心驱动外接口 (传入 GPIO 端口和引脚，实现完全解耦)
void    HX711_Init(GPIO_TypeDef *sck_port, uint16_t sck_pin, GPIO_TypeDef *dout_port, uint16_t dout_pin);
void    HX711_Tare(void);      // 去皮（上电自动清零）
int32_t HX711_ReadRaw(void);   // 读取 24 位原始 ADC 值
float   HX711_GetWeight(void); // 计算实际重量（单位：克）

#endif /* __HX711_H */
