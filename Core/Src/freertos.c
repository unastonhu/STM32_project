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

typedef struct {
    float   weight;        // 重量 (g)
    uint8_t dht11_hum;       // DHT11 湿度 (%)
    uint8_t dht11_temp;      // DHT11 温度 (C)
    int8_t  dht_status;    // DHT11 状态码 (用于排错)
    float   ds18b20_temp;  // DS18B20 温度 (C)

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
/* Definitions for Task_Temp */
osThreadId_t Task_TempHandle;
const osThreadAttr_t Task_Temp_attributes = {
  .name = "Task_Temp",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartMonitorTask(void *argument);
void StartLEDTask(void *argument);
void StartSonarTask(void *argument);
void StartWeightTask(void *argument);
void StartHumidityTask(void *argument);
void StartTempTask(void *argument);

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

  /* creation of Task_Weight */
  Task_WeightHandle = osThreadNew(StartWeightTask, NULL, &Task_Weight_attributes);

  /* creation of Task_Humidity */
  Task_HumidityHandle = osThreadNew(StartHumidityTask, NULL, &Task_Humidity_attributes);

  /* creation of Task_Temp */
  Task_TempHandle = osThreadNew(StartTempTask, NULL, &Task_Temp_attributes);

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
    
          printf("[  HX711  ] Weight : %.1f g\r\n", sysData.weight);
    
    if(sysData.dht_status == 1) {
          printf("[  DHT11  ]  Temp  : %d C  | Hum: %d %%\r\n", sysData.dht11_temp, sysData.dht11_hum);
    } else {
          printf("[  DHT11  ] Error Code: %d\r\n", sysData.dht_status);
    }
    
          printf("[ DS18B20 ]  Temp  : %.2f C\r\n", sysData.ds18b20_temp);
          

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
    osDelay(1000);

  }
  /* USER CODE END StartSonarTask */
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
    osDelay(1000);

    // 2. 核心换算并获取重量（克）
    float weight = HX711_GetWeight();
    
    // 3. 打印称重结果
    weight = (weight < 0.0f) ? 0.0f : weight; // 过滤负数抖动
    sysData.weight = weight; // 更新全局黑板数据，供监视器任务读取

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
    osDelay(500); // 先等一会，给系统和传感器腾出时间

   int8_t status = DHT11_Read_Data(&hum, &temp_dht);
    
    if(status == 1)
    {
        sysData.dht11_hum = hum;
        sysData.dht11_temp = temp_dht;
        sysData.dht_status = 1; // 成功读取
    }
    else
    {
        // 关键所在：这行会打印出它到底死在了哪里！
        printf("[DHT11 Task] GPIO Error Code: %d\r\n", status);
    }

    // 手册规定两次读取必须间隔 1 秒以上
    osDelay(5000);
    

  }
  /* USER CODE END StartHumidityTask */
}

/* USER CODE BEGIN Header_StartTempTask */
/**
* @brief Function implementing the Task_Temp thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTempTask */
void StartTempTask(void *argument)
{
  /* USER CODE BEGIN StartTempTask */
  (void)argument;
  MQ_Init();
  /* Infinite loop */
  for(;;)
  {
    
          // 2. 读取温度 (此时 DS18B20 里面的 osDelay 会让出 CPU，非常健康！)
    osDelay(2000);

      sysData.ds18b20_temp = DS18B20_GetTemp();

     
    

      // 5. RTOS 专属休眠
      osDelay(5000);

  }
  /* USER CODE END StartTempTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

