#ifndef __HCSR04_H
#define __HCSR04_H

#include "main.h"

// 初始化：传入定时器句柄和通道
void  HCSR04_Init(TIM_HandleTypeDef *htim, uint32_t channel);

// 触发测距：传入 Trig 的端口和引脚
void  HCSR04_StartTrigger(GPIO_TypeDef *TRIG_PORT, uint16_t TRIG_PIN);

// 获取距离
float HCSR04_GetDistance(void);

// 中断回调中转
void  HCSR04_TmrOverflowCallback(TIM_HandleTypeDef *htim);
void  HCSR04_CaptureCallback(TIM_HandleTypeDef *htim);

#endif /* __HCSR04_H */
