/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdio.h>



#include "ds18b20.h"
#include "mq_sensor.h"
#include "hcsr04.h"
#include "hx711.h"
#include "dht11.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */



extern TIM_HandleTypeDef htim4;






/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for LEDTask */
osThreadId_t LEDTaskHandle;
const osThreadAttr_t LEDTask_attributes = {
  .name = "LEDTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for SonarTask */
osThreadId_t SonarTaskHandle;
const osThreadAttr_t SonarTask_attributes = {
  .name = "SonarTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Weight */
osThreadId_t Task_WeightHandle;
const osThreadAttr_t Task_Weight_attributes = {
  .name = "Task_Weight",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Humidity */
osThreadId_t Task_HumidityHandle;
const osThreadAttr_t Task_Humidity_attributes = {
  .name = "Task_Humidity",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void StartWeightTask(void *argument);
void StartHumidityTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of LEDTask */
  LEDTaskHandle = osThreadNew(StartTask02, NULL, &LEDTask_attributes);

  /* creation of SonarTask */
  SonarTaskHandle = osThreadNew(StartTask03, NULL, &SonarTask_attributes);

  /* creation of Task_Weight */
  Task_WeightHandle = osThreadNew(StartWeightTask, NULL, &Task_Weight_attributes);

  /* creation of Task_Humidity */
  Task_HumidityHandle = osThreadNew(StartHumidityTask, NULL, &Task_Humidity_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;
  /* Infinite loop */
  for(;;)
  {
    
  //================================================================================
  // 1. 在任务一开始，启动 ADC DMA 搬运工
  MQ_Init();
  
  /* Infinite loop */
  for(;;)
  {
      // 2. 读取温度 (此时 DS18B20 里面的 osDelay 会让出 CPU，非常健康！)
    osDelay(1000);

      float temp = DS18B20_GetTemp();

      // 3. 直接从 DMA 数组里拿 4 个气体的电压，瞬间完成！
      float vol_mq3_1   = MQ_Get_Voltage(MQ3_1_CH);
      float vol_mq3_2   = MQ_Get_Voltage(MQ3_2_CH);
      float vol_mq135_1 = MQ_Get_Voltage(MQ135_1_CH);
      float vol_mq135_2 = MQ_Get_Voltage(MQ135_2_CH);

      float conc_mq3_1   = MQ3_Get_mgL(vol_mq3_1);
      float conc_mq3_2 = MQ3_Get_mgL(vol_mq3_2);
      float conc_mq135_1 = MQ135_Get_PPM(vol_mq135_1);
      float conc_mq135_2 = MQ135_Get_PPM(vol_mq135_2);
      // 4. 打印数据
      printf("Temp: %.2f C ||| MQ3_1: %.2f mg/L (%.2f V) | MQ3_2: %.2f mg/L (%.2f V)||| MQ135_1: %.2f PPM (%.2f V) | MQ135_2: %.2f PPM (%.2f V)\r\n", 
        temp, conc_mq3_1, vol_mq3_1, conc_mq3_2, vol_mq3_2, conc_mq135_1, vol_mq135_1, conc_mq135_2, vol_mq135_2);
      
      // 5. RTOS 专属休眠
      osDelay(5000);

    
  }}
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the LEDTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  (void)argument;
  /* Infinite loop */
  for(;;)
  {
    // 让 LED1 亮，LED2 灭
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
      osDelay(500); // 延时 500 毫秒，交出 CPU

      // 让 LED1 灭，LED2 亮
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
      osDelay(500); // 延时 500 毫秒，交出 CPU
  }
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
* @brief Function implementing the SonarTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask03 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  (void)argument;

  HCSR04_Init(&htim4, TIM_CHANNEL_1); // 先把定时器句柄和通道传给超声波模块

  /* Infinite loop */
  for(;;)
  {
    osDelay(2000);
    // 2. 触发超声波测距 (绑定 PB5)
    HCSR04_StartTrigger(GPIOB, GPIO_PIN_5);
    
    // 3. 等待声波返回 (强制等 60ms)
    osDelay(60); 
    
    // 4. 获取距离并打印
    float distance = HCSR04_GetDistance();
    if(distance > 0.0f)
    {
       printf("[Sonar] Dist: %.2f cm\r\n", distance);
    }

    // 5. 休息一下，开启下一次测距
    osDelay(5000);

  }
  /* USER CODE END StartTask03 */
}

/* USER CODE BEGIN Header_StartWeightTask */
/**
* @brief Function implementing the Task_Weight thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartWeightTask */
void StartWeightTask(void *argument)
{
  /* USER CODE BEGIN StartWeightTask */
  (void)argument;

  // 1. 初始化 HX711 模块 (绑定 PB6, PB7)
  HX711_Init(GPIOD,GPIO_PIN_0,GPIOD,GPIO_PIN_1);
  /* Infinite loop */
  for(;;)
  {
    osDelay(3000);
    // 2. 核心换算并获取重量（克）
    float weight = HX711_GetWeight();
    
    // 3. 打印称重结果
    printf("[Weight Task] Real Weight: %.1f g\r\n", weight);

    // 4. 重量不需要太频繁刷新，500ms 称一次，体验最好且省 CPU
    osDelay(5000);




    
  }
  /* USER CODE END StartWeightTask */
}

/* USER CODE BEGIN Header_StartHumidityTask */
/**
* @brief Function implementing the Task_Humidity thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartHumidityTask */
void StartHumidityTask(void *argument)
{
  /* USER CODE BEGIN StartHumidityTask */
  (void)argument;

 // 传给它我们刚配置好的串口 3 句柄


  uint8_t hum = 0;
  uint8_t temp_dht = 0;

// 初始化：绑定我们配置好的 PC0 引脚
  DHT11_Init(GPIOC, GPIO_PIN_0);

  /* Infinite loop */
  for(;;)
  {

   int8_t status = DHT11_Read_Data(&hum, &temp_dht);
    
    if(status == 1)
    {
        printf("[DHT11 Task] Hum: %d %%, Temp: %d C\r\n", hum, temp_dht);
    }
    else
    {
        // 关键所在：这行会打印出它到底死在了哪里！
        printf("[DHT11 Task] GPIO Error Code: %d\r\n", status);
    }

    // 手册规定两次读取必须间隔 1 秒以上
    osDelay(4000);
    

  }
  /* USER CODE END StartHumidityTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

