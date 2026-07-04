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
#include "i2c.h"
#include "tim.h"
#include "usart.h"


#include "ds18b20.h"
#include "hcsr04.h"
#include "hx711.h"
#include "dht11.h"
#include "sgp40.h"
#include "ens160_aht21.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct {

    float   weight;        // 重量 (g)
    int8_t  weight_status; // 称重状态 (1:正常, -1:掉线)

    float   ds18b20_temp;  // DS18B20 温度 (C)
    int8_t  ds18b20_status;// DS18B20 状态 (1:正常, -1:掉线)

    uint8_t dht11_hum;       // DHT11 湿度 (%)
    uint8_t dht11_temp;      // DHT11 温度 (C)
    int8_t  dht_status;    // DHT11 状态码 (用于排错)
    
    uint16_t sgp40_raw;    // SGP40 原始 VOC 信号 (Raw Signal)
    int8_t  sgp40_status;  // SGP40 状态码 (用于排错)
    int32_t voc_index;     // SGP40 官方算法解析后的 VOC 指数 (0~500)

    float    aht_temp;     // AHT21 高精度温度
    float    aht_hum;      // AHT21 高精度湿度
    uint16_t ens_tvoc;     // ENS160 总挥发性有机物 (ppb)
    uint16_t ens_eco2;     // ENS160 等效二氧化碳 (ppm)
    uint8_t  ens_aqi;      // ENS160 空气质量指数 (1~5)
    int8_t   env_status;   // 模块状态码


} SystemData_t;

// 实例化这块黑板（全局变量）
SystemData_t sysData = {0};

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
/* Definitions for Task_Monitor */
osThreadId_t Task_MonitorHandle;
const osThreadAttr_t Task_Monitor_attributes = {
  .name = "Task_Monitor",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_LED */
osThreadId_t Task_LEDHandle;
const osThreadAttr_t Task_LED_attributes = {
  .name = "Task_LED",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Sonar */
osThreadId_t Task_SonarHandle;
const osThreadAttr_t Task_Sonar_attributes = {
  .name = "Task_Sonar",
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
/* Definitions for Task_I2C */
osThreadId_t Task_I2CHandle;
const osThreadAttr_t Task_I2C_attributes = {
  .name = "Task_I2C",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartMonitorTask(void *argument);
void StartLEDTask(void *argument);
void StartSonarTask(void *argument);
void StartHumidityTask(void *argument);
void StartI2cTask(void *argument);

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
  /* creation of Task_Monitor */
  Task_MonitorHandle = osThreadNew(StartMonitorTask, NULL, &Task_Monitor_attributes);

  /* creation of Task_LED */
  Task_LEDHandle = osThreadNew(StartLEDTask, NULL, &Task_LED_attributes);

  /* creation of Task_Sonar */
  Task_SonarHandle = osThreadNew(StartSonarTask, NULL, &Task_Sonar_attributes);

  /* creation of Task_Humidity */
  Task_HumidityHandle = osThreadNew(StartHumidityTask, NULL, &Task_Humidity_attributes);

  /* creation of Task_I2C */
  Task_I2CHandle = osThreadNew(StartI2cTask, NULL, &Task_I2C_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartMonitorTask */
/**
  * @brief  Function implementing the Task_Monitor thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartMonitorTask */
void StartMonitorTask(void *argument)
{
  /* USER CODE BEGIN StartMonitorTask */
  (void)argument;
  /* Infinite loop */
  for(;;)
  {

    osDelay(5000);


          printf("\r\n============================ SYSTEM STATUS ====================================\r\n");
    
          // 称重 UI
      if (sysData.weight_status == 1) 
           printf("[  HX711  ] Weight : %.1f g\r\n", sysData.weight);

      else printf("[  HX711  ]  Error : Offline!\r\n");
    
    if(sysData.dht_status == 1) {
          printf("[  DHT11  ]  Temp  : %d C  | Hum: %d %%\r\n", sysData.dht11_temp, sysData.dht11_hum);
    } else {
          printf("[  DHT11  ]  Error : %d\r\n", sysData.dht_status);
    }
    
          // DS18B20 UI
      if (sysData.ds18b20_status == 1) 
           printf("[ DS18B20 ]  Temp  : %.2f C\r\n", sysData.ds18b20_temp);
      else printf("[ DS18B20 ]  Error : Offline!\r\n");
          
      if (sysData.sgp40_status == 1) {
          printf("[  SGP40  ] RawVOC : %u ticks  |  VOC Index : %ld\r\n", sysData.sgp40_raw, sysData.voc_index);
      } else {
          printf("[  SGP40  ]  Error : %d\r\n", sysData.sgp40_status);
      }

      
      if (sysData.env_status != -1) {
          printf("[  AHT21  ] Temp   : %.2f C    |  Hum : %.2f %%\r\n", sysData.aht_temp, sysData.aht_hum);
          
          // 然后再单独判断 ENS160 的状态
          if (sysData.env_status == 1) {
              printf("[  ENS160 ] TVOC   : %u ppb    | eCO2 : %u ppm   | AQI: %d\r\n", sysData.ens_tvoc, sysData.ens_eco2, sysData.ens_aqi);
          } else if (sysData.env_status == -2) {
              printf("[  ENS160 ] Data not ready yet (Warming up...)\r\n");
          }
      } else {
          // 只有返回 -1 时，才是连 AHT21 都彻底掉线了
          printf("[ ENV_MOD ] Error : AHT21 Offline!\r\n");
      }
      

          printf("====================================================================================\r\n");

  }
  /* USER CODE END StartMonitorTask */
}

/* USER CODE BEGIN Header_StartLEDTask */
/**
* @brief Function implementing the Task_LED thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartLEDTask */
void StartLEDTask(void *argument)
{
  /* USER CODE BEGIN StartLEDTask */
  (void)argument;
  /* Infinite loop */
  for(;;)
  {
    // 这个任务的唯一职责就是让两个 LED 灯交替闪烁，表示系统正在运行中
    // 让 LED1 亮，LED2 灭
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
      osDelay(500); // 延时 500 毫秒，交出 CPU

      // 让 LED1 灭，LED2 亮
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
      osDelay(500); // 延时 500 毫秒，交出 CPU

  }
  /* USER CODE END StartLEDTask */
}

/* USER CODE BEGIN Header_StartSonarTask */
/**
* @brief Function implementing the Task_Sonar thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSonarTask */
void StartSonarTask(void *argument)
{
  /* USER CODE BEGIN StartSonarTask */
  (void)argument;
  /* Infinite loop */

  HCSR04_Init(&htim4, TIM_CHANNEL_1); // 先把定时器句柄和通道传给超声波模块
  for(;;)
  {
    
     
    // 2. 触发超声波测距 (绑定 PB5)
    HCSR04_StartTrigger(GPIOB, GPIO_PIN_5);
    
    // 3. 等待声波返回 (强制等 60ms)
    osDelay(60); 
    
    // 4. 获取距离并打印
    float distance = HCSR04_GetDistance();
    if(distance > 0.0f)
    {
        printf("[ HC-SR04 ] Distance : %.1f cm\r\n", distance);
    }
    else
    {
        printf("[Sonar Task] Measurement Error\r\n");
    }

    // 5. 休息一下，开启下一次测距
    osDelay(500);

  }
  /* USER CODE END StartSonarTask */
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

 

// === 1. 统一在这里进行所有慢速传感器的初始化 ===
  DHT11_Init(GPIOC, GPIO_PIN_0);
  DS18B20_Init(); // 假设你的 DS18B20 在 PA8，请根据实际情况修改
  HX711_Init(GPIOD, GPIO_PIN_0, GPIOD, GPIO_PIN_1);
  // SGP40_Init(&hi2c1); // 预留给 SGP40

  /* Infinite loop */
  for(;;)
  {
     // 1. 智能测称重
      float w = HX711_GetWeight();
      if (w <= -999.0f) { // 捕获到故障码
          sysData.weight_status = -1; // 标记坏了
          sysData.weight = 0;
      } else {
          sysData.weight_status = 1;  // 标记正常
          sysData.weight = (w < 0.0f) ? 0.0f : w;
      }

      // 2. 智能测 DS18B20 (假设你把故障码设为了 -999)
      float t = DS18B20_GetTemp();
      if (t <= -999.0f) {
          sysData.ds18b20_status = -1;
      } else {
          sysData.ds18b20_status = 1;
          sysData.ds18b20_temp = t;
      }

      // 3. 测 DHT11 (本身就自带容错)
      uint8_t hum = 0, temp_dht = 0;
      sysData.dht_status = DHT11_Read_Data(&hum, &temp_dht);
      if(sysData.dht_status == 1) {
          sysData.dht11_hum = hum;
          sysData.dht11_temp = temp_dht;
      }
      osDelay(30000);
    

  }
  /* USER CODE END StartHumidityTask */
}

/* USER CODE BEGIN Header_StartI2cTask */
/**
* @brief Function implementing the Task_I2C thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartI2cTask */
void StartI2cTask(void *argument)
{
  /* USER CODE BEGIN StartI2cTask */
  (void)argument;

  SGP40_Init(&hi2c1);
  ENV_Module_Init(&hi2c1);
  // BME688_Init(&hi2c1); // 预留

  uint32_t task_tick = 0; // 用于心跳计数

  /* Infinite loop */
  for(;;)
  {

    task_tick++; // 心跳+1

 // =========================================================
      // ⏱️ 频段 1：[ 1Hz ] - 冰箱环境精密监控
      // =========================================================
      if (task_tick % 1 == 0) 
      {
          // 1. 调用咱们定稿的终极函数（它内部会自动测 AHT21 并喂给 ENS160）
        sysData.env_status = ENV_Module_ReadAll(
              &sysData.aht_temp, 
              &sysData.aht_hum, 
              &sysData.ens_tvoc, 
              &sysData.ens_eco2, 
              &sysData.ens_aqi
          );
          
          // 2. 只要返回值不是 -1，就说明 AHT21 没掉线，拿到了真实的冰箱温湿度！
          if (sysData.env_status != -1) 
          {
              // 喂 SGP40 (用新鲜出炉的真值做底层补偿)
              sysData.sgp40_status = SGP40_GetVOCIndex(
                  sysData.aht_hum,   
                  sysData.aht_temp,  
                  &sysData.sgp40_raw,         
                  &sysData.voc_index          
              );
          }
          else
          {
              // 🚨 故障处理：AHT21 彻底没拿到数据
              // 既不喂假数据，也不触发补偿，防止 SGP40 算法崩溃
              sysData.sgp40_status = -2; // 在黑板上标记：环境数据不可用
          }
      }


      // =========================================================
      // 任务底层心跳：严格锁定 1 秒钟休眠
      // 所有的传感器，全靠这 1 秒钟的心跳来驱动！
      // =========================================================
      osDelay(1000);
  }
  /* USER CODE END StartI2cTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

