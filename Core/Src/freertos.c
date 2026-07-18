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
#include "dma.h"
#include "sdio.h"

#include "ds18b20.h"
#include "hcsr04.h"
#include "hx711.h"
#include "dht11.h"

#include "sgp40.h"
#include "ens160_aht21.h"

#include "bme68x.h"
#include "bme688_port.h"
#include "bme68x_defs.h"

#include "w25q64.h"
#include "fatfs.h"
#include "ff.h"
#include "system_data.h"

#include "usbd_cdc_if.h"

#include "control.h"
#include "usb_reporter.h"

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

uint8_t usb_rx_buffer[64] = {0};
uint8_t usb_rx_flag = 0;
uint32_t usb_rx_len = 0;

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
/* Definitions for Task_USB */
osThreadId_t Task_USBHandle;
const osThreadAttr_t Task_USB_attributes = {
  .name = "Task_USB",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartMonitorTask(void *argument);
void StartLEDTask(void *argument);
void StartSonarTask(void *argument);
void StartHumidityTask(void *argument);
void StartI2cTask(void *argument);
void StartUSBTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
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

  /* creation of Task_USB */
  Task_USBHandle = osThreadNew(StartUSBTask, NULL, &Task_USB_attributes);

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
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartMonitorTask */
  (void)argument;
  /* Infinite loop */
  for(;;)
  {

    // 1. 打印系统状态
    System_PrintStatus(&sysData);

    // 2. 每隔 5 秒打印一次
    osDelay(5000);


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

  W25Q64_Init();

  sysData.flash1.id = W25Q64_ReadID(0);
  if (sysData.flash1.id == 0xEF4017) {sysData.flash1.rw_test = W25Q64_SanityCheck(0);
  }


   sysData.flash2.id = W25Q64_ReadID(1);
   if (sysData.flash2.id == 0xEF4017) sysData.flash2.rw_test = W25Q64_SanityCheck(1);



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
    
    // ==========================================
      // 读取红外对射模块 (D0)
      // ==========================================
      // 根据你模块板子上的旋钮调校，通常有遮挡时 D0 输出高电平 (1) 或低电平 (0)
      // 如果你发现屏幕上显示的 CLEAR 和 BLOCKED 是反的，在 HAL_GPIO_ReadPin 前面加个感叹号 ! 即可反转逻辑

      sysData.ir.ir1_blocked = HAL_GPIO_ReadPin(GPIOD, IR_D1_Pin);
      sysData.ir.ir2_blocked = HAL_GPIO_ReadPin(GPIOD, IR_D2_Pin);
      sysData.ir.status = sysData.ir.ir2_blocked && sysData.ir.ir1_blocked; // 读取同时遮挡和同时不遮挡的状态码，1:都被遮挡, 0:都没被遮挡, 其他情况为中间状态


     
    // 2. 触发超声波测距 (绑定 PB5)
    HCSR04_StartTrigger(GPIOB, GPIO_PIN_5);
    
    // 3. 等待声波返回 (强制等 60ms)
    osDelay(60); 
    
    float temp_dist  = 0.0f; // 临时变量，存储测距结果
    // 4. 获取距离并打印
    float distance = HCSR04_GetDistance();
    if(distance > 0.0f)
    {
        printf("[ HC-SR04 ] Distance : %.1f cm\r\n", distance);
        temp_dist  = distance;
    }
    else
    {
        printf("[Sonar Task] Measurement Error\r\n");
        temp_dist = -1.0f; // 错误码
    }

    sysData.ui.distance = temp_dist;

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
 

  /* Infinite loop */
  for(;;)
  {
     // 1. 智能测称重
      float w = HX711_GetWeight();
      if (w <= -999.0f) { // 捕获到故障码
          sysData.hx711.status = -1; // 标记坏了
          sysData.hx711.weight = 0;
      } else {
          sysData.hx711.status = 1;  // 标记正常
          sysData.hx711.weight = (w < 0.0f) ? 0.0f : w;
      }

      // 2. 智能测 DS18B20 (假设你把故障码设为了 -999)
      float t = DS18B20_GetTemp();
      if (t <= -999.0f) {
          sysData.ds18b20.status = -1;
      } else {
          sysData.ds18b20.status = 1;
          sysData.ds18b20.temp = t;
      }

      // 3. 测 DHT11 (本身就自带容错)
      uint8_t hum = 0, temp_dht = 0;
      sysData.dht11.status = DHT11_Read_Data(&hum, &temp_dht);
      if(sysData.dht11.status == 1) {
          sysData.dht11.hum = hum;
          sysData.dht11.temp = temp_dht;
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
  BME688_Port_Init(&hi2c1);

  

  uint32_t task_tick = 0; // 用于心跳计数

  /* Infinite loop */
  for(;;)
  {

    task_tick++; // 心跳+1

 // =========================================================
      //  频段 1：[ 1Hz ] - 冰箱环境精密监控
      // =========================================================
      if (task_tick % 1 == 0) 
      {
          // 1. 调用咱们定稿的终极函数（它内部会自动测 AHT21 并喂给 ENS160）
        sysData.env.status = ENV_Module_ReadAll(
              &sysData.env.aht_temp, 
              &sysData.env.aht_hum, 
              &sysData.env.ens_tvoc, 
              &sysData.env.ens_eco2, 
              &sysData.env.ens_aqi
          );
          
          // 新增：秒表逻辑
          if (sysData.env.status == -2) {
              sysData.env.ens_warmup_sec++; // 如果在热身，秒表+1
          } else if (sysData.env.status == 1) {
              sysData.env.ens_warmup_sec = 0; // 如果出数据了，秒表清零
          }
  

          // 2. 只要返回值不是 -1，就说明 AHT21 没掉线，拿到了真实的冰箱温湿度！
          if (sysData.env.status != -1) 
          {
              // 喂 SGP40 (用新鲜出炉的真值做底层补偿)
              sysData.sgp40.status = SGP40_GetVOCIndex(
                  sysData.env.aht_hum,   
                  sysData.env.aht_temp,  
                  &sysData.sgp40.raw,         
                  &sysData.sgp40.voc_index          
              );
          }
          else
          {
              //故障处理：AHT21 彻底没拿到数据
              // 既不喂假数据，也不触发补偿，防止 SGP40 算法崩溃
              sysData.sgp40.status = -2; // 在黑板上标记：环境数据不可用
          }


      }

      // =========================================================

      // =========================================================
      //  频段 3：[ 低频区 - 30秒/次 ] 
      // 专供：BME688 (测气压、环境底噪气体阻值)
      // =========================================================
   if (task_tick % 30 == 0)
   {
       sysData.bme688.status = BME688_Port_Read(
           &sysData.bme688.temp, 
           &sysData.bme688.hum, 
           &sysData.bme688.press, 
           &sysData.bme688.gas_res
       );
   }

      // =========================================================
      // 任务底层心跳：严格锁定 1 秒钟休眠
      // 所有的传感器，全靠这 1 秒钟的心跳来驱动！
      // =========================================================
      osDelay(1000);
  }
  /* USER CODE END StartI2cTask */
}

/* USER CODE BEGIN Header_StartUSBTask */
/**
* @brief Function implementing the Task_USB thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUSBTask */
void StartUSBTask(void *argument)
{
  /* USER CODE BEGIN StartUSBTask */
  /* Infinite loop */
   (void)argument;

// 架构师级防御：使用 static 关键字把 1024 字节的巨型缓冲区从任务栈移到全局 BSS 段
  // 彻底杜绝 FreeRTOS 任务栈溢出死机的问题！
  static char usb_tx_buf[1024]; 
  uint16_t tx_len;
  
  // 初始化配置
  sysData.slow_interval_ms = 30000; // 默认 30秒 慢信号档位
  sysData.ozone_is_locked = 0;
  
  TickType_t last_slow_tick = xTaskGetTickCount();
  TickType_t current_tick;

   // 1. 初始化通讯部时间戳
  USB_Reporter_Init(); 

  // 新增：唤醒 AI 电子鼻大脑
  Control_ENose_Init();

  for(;;)
  {

      if (usb_rx_ready == 1) {
          USB_Command_Parser(usb_rx_buf); // 调用刚才封装的专属函数
          usb_rx_ready = 0;               // 清理现场，接收下一波
      }

      Control_ENose_Tick();
     
      Control_Update_Routine();

      //3. 呼叫通讯大队 (智能分发 JSON 快慢信号)
      USB_Reporter_Routine();   

      // 4. 完美保持极高实时性，绝不阻塞！
      osDelay(1000);

      
         
      }
      
  /* USER CODE END StartUSBTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

