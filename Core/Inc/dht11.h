#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"

// 纯参数化接口
void   DHT11_Init(GPIO_TypeDef *gpio_port, uint16_t gpio_pin);
int8_t DHT11_Read_Data(uint8_t *humidity, uint8_t *temperature);

#endif /* __DHT11_H */
