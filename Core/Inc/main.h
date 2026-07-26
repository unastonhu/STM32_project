/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* 在普通任务上下文解析 K230 完整帧；UART 中断只负责收字节。 */
void K230_ProcessPendingFrame(void);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define DHT11_DATA_Pin GPIO_PIN_0
#define DHT11_DATA_GPIO_Port GPIOC
#define cool_fans_Pin GPIO_PIN_2
#define cool_fans_GPIO_Port GPIOA
#define cool_fansA3_Pin GPIO_PIN_3
#define cool_fansA3_GPIO_Port GPIOA
#define duct_fans_Pin GPIO_PIN_4
#define duct_fans_GPIO_Port GPIOA
#define duct_fansA5_Pin GPIO_PIN_5
#define duct_fansA5_GPIO_Port GPIOA
#define LED_1_Pin GPIO_PIN_6
#define LED_1_GPIO_Port GPIOA
#define LED_2_Pin GPIO_PIN_7
#define LED_2_GPIO_Port GPIOA
#define W25_01_CS_Pin GPIO_PIN_4
#define W25_01_CS_GPIO_Port GPIOC
#define W25_02_CS_Pin GPIO_PIN_5
#define W25_02_CS_GPIO_Port GPIOC
#define ozone_Pin GPIO_PIN_1
#define ozone_GPIO_Port GPIOB
#define uv_lamp_Pin GPIO_PIN_2
#define uv_lamp_GPIO_Port GPIOB
#define IR_D2_Pin GPIO_PIN_13
#define IR_D2_GPIO_Port GPIOD
#define IR_D1_Pin GPIO_PIN_14
#define IR_D1_GPIO_Port GPIOD
#define ds28b20_DQ_Pin GPIO_PIN_8
#define ds28b20_DQ_GPIO_Port GPIOA
#define HX711_SCK_Pin GPIO_PIN_0
#define HX711_SCK_GPIO_Port GPIOD
#define HX711_DOUT_Pin GPIO_PIN_1
#define HX711_DOUT_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
