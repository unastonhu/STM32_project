#ifndef __DS18B20_H__
#define __DS18B20_H__

#include "main.h"
#include <stdint.h>

uint8_t DS18B20_Init(void);
float DS18B20_GetTemp(void);

#endif /* __DS18B20_H__ */
