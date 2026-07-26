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
#include "fridge_weight_engine.h"

#include "dht11.h"

#include "sgp40.h"
#include "ens160_aht21.h"

#include "bme68x.h"
#include "bme688_port.h"
#include "bme68x_defs.h"
#include "bme688_bsec_app.h"

#include "w25q64.h"
#include "flash_manager.h"
#include "flash_worker.h"
#include "sample_library.h"
#include "sys_time.h"
#include "fatfs.h"
#include "ff.h"
#include "system_data.h"

#include "usbd_cdc_if.h"

#include "control.h"
#include "usb_reporter.h"

#include "enose.h"
#include "enose_frame_buffer.h"
#include "ai_feature_extractor.h"
#include "prototype_head.h"
#include "prototype_worker.h"
#include "data_export.h"

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

#define ENOSE_TASK_PERIOD_MS  1000U
#define ENOSE_LOG_INTERVAL_MS 60000U

// [新增]: 定义 I2C 的公共钥匙（互斥锁）
osMutexId_t i2c_mutex;
osMutexId_t flash_mutex;

extern SystemData_t sysData;
FridgeWeightEngine_t g_weight_engine;
static volatile uint8_t s_usb_device_ready;

/* USER CODE END Variables */
/* Definitions for Task_Monitor */
osThreadId_t Task_MonitorHandle;
const osThreadAttr_t Task_Monitor_attributes = {
  .name = "Task_Monitor",
  .stack_size = 512 * 4,
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
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_I2C */
osThreadId_t Task_I2CHandle;
const osThreadAttr_t Task_I2C_attributes = {
  .name = "Task_I2C",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_USB */
osThreadId_t Task_USBHandle;
const osThreadAttr_t Task_USB_attributes = {
  .name = "Task_USB",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Task_BSEC */
osThreadId_t Task_BSECHandle;
const osThreadAttr_t Task_BSEC_attributes = {
  .name = "Task_BSEC",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Enose */
osThreadId_t Task_EnoseHandle;
const osThreadAttr_t Task_Enose_attributes = {
  .name = "Task_Enose",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Flash */
osThreadId_t Task_FlashHandle;
const osThreadAttr_t Task_Flash_attributes = {
  .name = "Task_Flash",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_Prototype */
osThreadId_t Task_PrototypeHandle;
const osThreadAttr_t Task_Prototype_attributes = {
  .name = "Task_Prototype",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

static void System_Startup_Routine(void);

/* USER CODE END FunctionPrototypes */

void StartMonitorTask(void *argument);
void StartLEDTask(void *argument);
void StartSonarTask(void *argument);
void StartHumidityTask(void *argument);
void StartI2cTask(void *argument);
void StartUSBTask(void *argument);
void StartBSECTask(void *argument);
void StartEnoseTask(void *argument);
void StartFlashTask(void *argument);
void StartPrototypeTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /*
   * Restore all Flash-backed state before any task can observe sysData.
   * Keep this call inside the CubeMX user section.
   */
  System_Startup_Routine();

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */

// [新增]: i2c互斥锁
  const osMutexAttr_t i2c_mutex_attr = {
    .name = "i2c_mutex",
  };
  i2c_mutex = osMutexNew(&i2c_mutex_attr);

  const osMutexAttr_t flash_mutex_attr = {
    .name = "flash_mutex",
  };
  flash_mutex = osMutexNew(&flash_mutex_attr);

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

  /* creation of Task_BSEC */
  Task_BSECHandle = osThreadNew(StartBSECTask, NULL, &Task_BSEC_attributes);

  /* creation of Task_Enose */
  Task_EnoseHandle = osThreadNew(StartEnoseTask, NULL, &Task_Enose_attributes);

  /* creation of Task_Flash */
  Task_FlashHandle = osThreadNew(StartFlashTask, NULL, &Task_Flash_attributes);

  /* creation of Task_Prototype */
  Task_PrototypeHandle = osThreadNew(StartPrototypeTask, NULL, &Task_Prototype_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /*
   * CubeMX 已负责创建两个任务。这里只初始化任务运行前需要的资源，
   * 禁止再次 osThreadNew，否则会产生两个 Flash/原型任务。
   */
  (void)FlashWorker_Init();
  (void)DataExport_Init();
  PrototypeWorker_AttachTask(Task_PrototypeHandle);
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
  /*
   * Cube 把 USB Device 初始化放在本普通优先级任务里，而 Task_USB 是
   * AboveNormal。初始化完成后再发布门闩，禁止高优先级任务提前解引用
   * 尚未创建的 CDC class data。
   */
  s_usb_device_ready = 1U;
  TickType_t last_wake_tick = xTaskGetTickCount();
  /* Infinite loop */
  for(;;)
  {

    // 1. 打印系统状态
    System_PrintStatus(&sysData);

    // 2. 固定每 5 秒打印一次，不把本轮串口输出耗时累积到下一周期
    vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(5000U));


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
  TickType_t last_wake_tick = xTaskGetTickCount();
  for(;;)
  {
    
    // ==========================================
      // 读取红外对射模块 (D0)
      // ==========================================
      // 根据你模块板子上的旋钮调校，通常有遮挡时 D0 输出高电平 (1) 或低电平 (0)
      // 如果你发现屏幕上显示的 CLEAR 和 BLOCKED 是反的，在 HAL_GPIO_ReadPin 前面加个感叹号 ! 即可反转逻辑

      sysData.ir.ir1_blocked = HAL_GPIO_ReadPin(GPIOD, IR_D1_Pin);
      sysData.ir.ir2_blocked = HAL_GPIO_ReadPin(GPIOD, IR_D2_Pin);
      /*
       * 两路对射采用保守 OR 策略：任一路仍被遮挡就认为门未完全打开；
       * 只有两路都无遮挡时 status=0，电子鼻才进入开门暂停状态。
       * 这里不改变原来的快速采样频率和信号极性。
       */
      sysData.ir.status =
          sysData.ir.ir2_blocked || sysData.ir.ir1_blocked;


     
    // 2. 触发超声波测距 (绑定 PB5)
    HCSR04_StartTrigger(GPIOB, GPIO_PIN_5);
    
    // 3. 等待声波返回 (强制等 60ms)
    osDelay(60); 
    
    float temp_dist  = 0.0f; // 临时变量，存储测距结果
    // 4. 获取距离；FAST/状态看板负责对外报告，不在 2 Hz 任务里刷屏
    float distance = HCSR04_GetDistance();
    if(distance > 0.0f)
    {
        temp_dist  = distance;
    }
    else
    {
        temp_dist = -1.0f; // 错误码
    }

    sysData.ui.distance = temp_dist;

    // 5. 固定 500ms 周期；上面的 60ms 回波等待包含在本周期内
    vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(500U));

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
 
  // 重量引擎已在 System_Startup_Routine() 中初始化并恢复锚点，禁止在此清空
  int32_t slow_sensor_counter = 0; // 慢速传感器计数器
  TickType_t last_wake_tick = xTaskGetTickCount();

  
/* Infinite loop */
for(;;)
{
    uint32_t now_sec = HAL_GetTick() / 1000;

    /*
     * UART2 中断只收集 K230 字节；字符串解析放在这个 5 Hz 普通任务，
     * 既能及时刷新视觉结果，也不会占用 FAST 所在的高优先级 USB 任务。
     */
    K230_ProcessPendingFrame();

    // =========================================================
    //  频段 A：[ 200ms 高频 (5Hz) ] - 称重滤波、消抖与开门阶跃捕获
    // =========================================================
    float raw_w = HX711_GetWeight();
    
    if (raw_w <= -900.0f) { 
        // 捕获到 HX711 底层超时/断线故障码
        sysData.hx711.status = -1; 
    } else {
        // 1. 喂入中值+滑动窗口复合滤波器 (消除压缩机震动抖动)
        taskENTER_CRITICAL();
        float filtered_w = FridgeWeight_UpdateFilter(&g_weight_engine, raw_w);

        // 2. 实时捕获开门状态下的拿放动作 (`sysData.ir.status == 0` 为开门)
        uint8_t door_is_open = (sysData.ir.status == 0);
        sysData.hx711.weight =
            (filtered_w < 0.0f) ? 0.0f : filtered_w;
        sysData.hx711.status = 1;
        FridgeWeight_ProcessDoorOpen(&g_weight_engine, door_is_open, now_sec);
        taskEXIT_CRITICAL();
    }

    // =========================================================
    //  频段 B：[ 30秒 低频 ] - DHT11 & DS18B20 环境温湿度采样
    //  (200ms * 150 次 = 30000ms = 30秒)
    // =========================================================
    slow_sensor_counter++;
    if (slow_sensor_counter >= 150)
    {
        slow_sensor_counter = 0; // 清零重新计数

        // 1. 智能测 DS18B20
        float t = DS18B20_GetTemp();
        taskENTER_CRITICAL();
        if (t <= -900.0f) {
            sysData.ds18b20.status = -1;
        } else {
            sysData.ds18b20.status = 1;
            sysData.ds18b20.temp = t;
        }
        taskEXIT_CRITICAL();

        // 2. 测 DHT11
        uint8_t hum = 0, temp_dht = 0;
        int8_t dht_status = DHT11_Read_Data(&hum, &temp_dht);
        taskENTER_CRITICAL();
        sysData.dht11.status = dht_status;
        if(dht_status == 1) {
            sysData.dht11.hum = hum;
            sysData.dht11.temp = temp_dht;
        }
        taskEXIT_CRITICAL();
    }

    // =========================================================
    // 核心心跳：锁定 200ms 休眠，保证称重高频滤波响应！
    // =========================================================
    vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(200U));
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

// SGP40/ENV 初始化函数直接访问 HAL，因此在外层持有总线锁。
extern osMutexId_t i2c_mutex; // 确保能引用到外部定义的锁
osMutexAcquire(i2c_mutex, osWaitForever);

SGP40_Init(&hi2c1);
ENV_Module_Init(&hi2c1);

osMutexRelease(i2c_mutex);

/*
 * BME688 驱动的 read/write 回调内部自己持有 i2c_mutex。
 * 禁止在这里套外层锁，否则非递归 mutex 会被同一任务二次获取并永久自锁。
 */
sysData.bme688.status =
    BME688_Port_Init(&hi2c1) == 1 ? 0 : -1;

uint32_t task_tick = 0; // 用于 BME688 初始化失败后的低频重试
TickType_t last_wake_tick = xTaskGetTickCount();

/* Infinite loop */
for(;;)
{
task_tick++; // 心跳+1

/*
 * BME688 上电偶发未就绪时每 5 秒重试；不阻塞 ENS160/SGP40 的 1 Hz 采样。
 * BSEC 任务会等待 IsReady，绝不会访问半初始化的 bme_dev。
 */
if (!BME688_Port_IsReady() && (task_tick % 5U) == 0U) {
    sysData.bme688.status =
        BME688_Port_Init(&hi2c1) == 1 ? 0 : -1;
}

// =========================================================
//  频段 1：[ 1Hz ] - 冰箱环境精密监控
// =========================================================
{
    float env_temp = 0.0f;
    float env_hum = 0.0f;
    uint16_t env_tvoc = 0U;
    uint16_t env_eco2 = 0U;
    uint8_t env_aqi = 0U;
    uint16_t sgp_raw = 0U;
    int32_t sgp_voc_index = 0;
    int8_t env_status;
    int8_t sgp_status;

    // [新增锁]: 拿钥匙，准备独占 I2C！
    osMutexAcquire(i2c_mutex, osWaitForever);

    /*
     * 先写局部变量，I2C 全部完成后再一次性发布到 sysData。
     * 这样电子鼻不会读到“温度已更新但 TVOC 仍是上一秒”的半帧数据。
     */
    env_status = ENV_Module_ReadAll(
        &env_temp,
        &env_hum,
        &env_tvoc,
        &env_eco2,
        &env_aqi
    );

    // 2. 只要返回值不是 -1，就说明 AHT21 没掉线，拿到了真实的冰箱温湿度！
    if (env_status != -1)
    {
        // 喂 SGP40 (用新鲜出炉的真值做底层补偿)
        sgp_status = SGP40_GetVOCIndex(
            env_hum,
            env_temp,
            &sgp_raw,
            &sgp_voc_index
        );
    }
    else
    {
        //故障处理：AHT21 彻底没拿到数据
        // 既不喂假数据，也不触发补偿，防止 SGP40 算法崩溃
        sgp_status = -2;
    }

    // [新增锁]: 操作完毕，开门交出 I2C 钥匙！
    osMutexRelease(i2c_mutex);

    /* 发布完整的一秒传感器快照；临界区内不执行任何 HAL 调用。 */
    taskENTER_CRITICAL();
    sysData.env.aht_temp = env_temp;
    sysData.env.aht_hum = env_hum;
    sysData.env.ens_tvoc = env_tvoc;
    sysData.env.ens_eco2 = env_eco2;
    sysData.env.ens_aqi = env_aqi;
    sysData.env.status = env_status;
    if (env_status == -2) {
        sysData.env.ens_warmup_sec++;
    } else if (env_status == 1) {
        sysData.env.ens_warmup_sec = 0U;
    }

    sysData.sgp40.status = sgp_status;
    if (sgp_status == 1) {
        sysData.sgp40.raw = sgp_raw;
        sysData.sgp40.voc_index = sgp_voc_index;
    }
    taskEXIT_CRITICAL();
}

// =========================================================
//  频段 3：[ 低频区 - 30秒/次 ] 
// 专供：BME688 (测气压、环境底噪气体阻值)
// =========================================================
/*if (task_tick % 30 == 0)
{
 sysData.bme688.status = BME688_Port_Read(
     &sysData.bme688.temp, 
     &sysData.bme688.hum, 
     &sysData.bme688.press, 
     &sysData.bme688.gas_res
 );
}*/ 

// =========================================================
// [新增逻辑]: 心跳清零机制，实现 60 秒的固定轮回
// =========================================================
if (task_tick >= 60) 
{
    task_tick = 0;
}

// =========================================================
// 任务底层心跳：严格锁定 1 秒钟休眠
// 所有的传感器，全靠这 1 秒钟的心跳来驱动！
// =========================================================
vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(1000U));


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
  TickType_t last_wake_tick = xTaskGetTickCount();

  while (s_usb_device_ready == 0U) {
      osDelay(10U);
  }

  // 初始化配置
  sysData.ozone_is_locked = 0;

  /*
   * Reporter 默认把 SLOW 设为 10 秒；FAST 和 USB 指令处理固定为 20 ms。
   * 两个周期相互独立，不能再用 1 秒任务延时把“50 Hz FAST”降成 1 Hz。
   */
  USB_Reporter_Init();

  for(;;)
  {

      if (usb_rx_ready == 1) {
          USB_Command_Parser(usb_rx_buf); // 调用刚才封装的专属函数
          usb_rx_ready = 0;               // 清理现场，接收下一波
      }

      Control_Update_Routine();

      //3. 呼叫通讯大队 (智能分发 JSON 快慢信号)
      USB_Reporter_Routine();   

      // 4. 固定 50 Hz，vTaskDelayUntil 可避免每轮执行耗时累积成周期漂移。
      vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(20));

      }
      
  /* USER CODE END StartUSBTask */
}

/* USER CODE BEGIN Header_StartBSECTask */
/**
* @brief Function implementing the Task_BSEC thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartBSECTask */
void StartBSECTask(void *argument)
{
  /* USER CODE BEGIN StartBSECTask */

  /*
   * Task_I2C 负责初始化 bme_dev。两个任务由调度器并发启动，
   * 所以必须等初始化完成，避免 BSEC 抢先访问空设备结构。
   */
  while (!BME688_Port_IsReady()) {
    osDelay(100);
  }

  // BME688_BSEC_Task 内部自带 while(1)，正常情况下不会返回。
  BME688_BSEC_Task(argument); 



  /* Infinite loop */
  for(;;)
  {
    osDelay(10000);
  }
  /* USER CODE END StartBSECTask */
}

/* USER CODE BEGIN Header_StartEnoseTask */
/**
 * @brief 电子鼻主运行任务 (运行周期: 1000ms / 1Hz)
 * @param argument: FreeRTOS 任务传入参数
 */
/* USER CODE END Header_StartEnoseTask */
void StartEnoseTask(void *argument)
{
  /* USER CODE BEGIN StartEnoseTask */
  (void)argument;

  // 电子鼻及其 Flash 参考集已在创建任务前完成初始化和恢复
  uint32_t last_tick = HAL_GetTick();
  uint32_t last_log_tick = last_tick;
  TickType_t last_wake_tick = xTaskGetTickCount();

  /* Infinite loop */
  for(;;)
  {
    uint32_t now_ms = HAL_GetTick();

    // 计算距上一拍的时间间隔（单位：分钟）
    float dt_min = (float)(now_ms - last_tick) / 60000.0f;
    if (dt_min < 1e-4f) {
        dt_min = 1.0f / 60.0f; // 兜底默认 1 秒 (1/60 分钟)
    }
    last_tick = now_ms;

    // 3. 生成一帧同步数据，并写入 1 Hz RAM 环形缓冲
    // [0] SGP40  SRAW (原始阻值/码值)
    // [1] ENS160 TVOC
    // [2] ENS160 eCO2
    // [3] BME688 gas_res
    // [4] HX711  weight (经过滑动滤波后的当前物理总重)
    ENoseFrame_t frame = {0};
    frame.timestamp_ms = now_ms;
    frame.raw[0] = (float)sysData.sgp40.raw;
    frame.raw[1] = (float)sysData.env.ens_tvoc;
    frame.raw[2] = (float)sysData.env.ens_eco2;
    frame.raw[3] = (float)sysData.bme688.gas_res;
    frame.raw[4] = sysData.hx711.weight;
    frame.door_state = (uint8_t)sysData.ir.status;

    if (sysData.sgp40.status == 1) {
      frame.valid_mask |= ENOSE_FRAME_VALID_SGP40;
    }
    if (sysData.env.status == 1) {
      frame.valid_mask |= ENOSE_FRAME_VALID_ENS_TVOC;
      frame.valid_mask |= ENOSE_FRAME_VALID_ENS_ECO2;
    }
    if (sysData.bme688.status == 1) {
      frame.valid_mask |= ENOSE_FRAME_VALID_BME688;
    }
    if (sysData.hx711.status == 1) {
      frame.valid_mask |= ENOSE_FRAME_VALID_HX711;
    }

    ENoseFrameBuffer_Push(&frame);
    if (sysData.flash2.status == 1) {
      (void)FlashWorker_EnqueueHistoryFrame(&frame);
    }

    /*
     * Cube.AI 周期推理已交给低优先级 Task_Prototype。
     * 本任务只负责严格的 1 Hz 同步采样、状态机和 Flash 入队，避免正式模型
     * 变大后推理耗时抖动原始数据时间轴。
     */

    // 4. 只有五通道完整有效时才允许状态机吸收本帧并更新判定
    ENose_State_t current_state;
    if (frame.valid_mask == ENOSE_FRAME_VALID_ALL) {
        current_state =
            ENose_Tick(&sysData.enose, frame.raw, now_ms, dt_min);
    } else {
        /*
         * 任一输入掉线时禁止把旧值/零值送入基线与规则分类。
         * UNKNOWN 会在 Control_ENose_Tick() 中触发执行器安全关停。
         */
        sysData.enose.state = ENOSE_UNKNOWN;
        current_state = ENOSE_UNKNOWN;
    }

    // 5. 根据同一份电子鼻状态更新自动控制策略
    Control_ENose_Tick();

    // 6. 仅在数据有效、判定有效且 Flash 在线时定时追加日志
    if (sysData.flash1.status == 1 &&
        current_state != ENOSE_UNKNOWN &&
        frame.valid_mask == ENOSE_FRAME_VALID_ALL &&
        (uint32_t)(now_ms - last_log_tick) >= ENOSE_LOG_INTERVAL_MS) {
        bool queued = FlashWorker_EnqueueEnoseLog(
            &sysData.enose,
            sysData.env.aht_temp,
            sysData.env.aht_hum,
            sysData.bme688.food_spoilage_risk,
            sysData.enose.actual_total_loss_g
        );
        if (queued) {
          last_log_tick = now_ms;
        }
    }

    // 固定 1Hz 调度，避免任务执行时间逐拍累积到采样周期
    vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(ENOSE_TASK_PERIOD_MS));
  }
  /* USER CODE END StartEnoseTask */
}

/* USER CODE BEGIN Header_StartFlashTask */
/**
* @brief Function implementing the Task_Flash thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartFlashTask */
void StartFlashTask(void *argument)
{
  /* USER CODE BEGIN StartFlashTask */
  /*
   * CubeMX 管理任务句柄、优先级和栈；真正的无限循环放在 Worker 中，
   * 这样以后重新生成代码仍会保留此 USER CODE。
   */
  FlashWorker_Task(argument);
  /* USER CODE END StartFlashTask */
}

/* USER CODE BEGIN Header_StartPrototypeTask */
/**
* @brief Function implementing the Task_Prototype thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartPrototypeTask */
void StartPrototypeTask(void *argument)
{
  /* USER CODE BEGIN StartPrototypeTask */
  /* 低优先级等待重建请求，2 秒合并连续的样本编辑操作。 */
  PrototypeWorker_Task(argument);
  /* USER CODE END StartPrototypeTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

static void System_Startup_Routine(void)
{
  float restored_weight_anchor = 0.0f;

  SysTime_Init();

  /*
   * SPI2/GPIO 已由 main() 初始化，此处只绑定器件并完成健康检查。
   * Flash 1 保存系统配置、电子鼻参考集和环形日志。
   */
  W25Q64_Init();

  sysData.flash1.id = W25Q64_ReadID(0);
  if (sysData.flash1.id == 0xEF4017U) {
    sysData.flash1.rw_test = W25Q64_SanityCheck(0);
    sysData.flash1.status = (sysData.flash1.rw_test == 1) ? 1 : 0;
  } else {
    sysData.flash1.status = 0;
  }

  sysData.flash2.id = W25Q64_ReadID(1);
  if (sysData.flash2.id == 0xEF4017U) {
    sysData.flash2.rw_test = W25Q64_SanityCheck(1);
    sysData.flash2.status = (sysData.flash2.rw_test == 1) ? 1 : 0;
  } else {
    sysData.flash2.status = 0;
  }

  // 先建立确定的 RAM 默认状态，再用有效 Flash 数据覆盖
  FridgeWeight_Init(&g_weight_engine);
  /*
   * 两路 IR 第一次真实采样前先按“门关闭”处理，防止同优先级任务启动
   * 顺序变化时，称重任务把上电初始重量误识别为一次开门放入动作。
   */
  sysData.ir.status = 1;
  ENose_Init(&sysData.enose);
  sysData.enose.scale[4] = 75.0f;
  ENose_AttachWeightEngine(&sysData.enose, &g_weight_engine);
  ENoseFrameBuffer_Init();

  /*
   * 在任务开始运行前创建 Cube.AI 实例。这样 StartEnoseTask 第一次做实时
   * 分类时网络一定已经就绪；初始化失败只会禁用动态分类，不阻塞其他功能。
   */
  (void)AIFeatureExtractor_Init();

  if (sysData.flash1.status == 1) {
    FlashMgr_Init();

    if (FlashMgr_LoadSysState(NULL, NULL, &restored_weight_anchor)) {
      g_weight_engine.base_anchor_weight = restored_weight_anchor;
      g_weight_engine.last_stable_w = restored_weight_anchor;
      g_weight_engine.last_sample_w = restored_weight_anchor;
    }

    FlashMgr_LoadEnoseClasses(&sysData.enose);
    (void)SampleLibrary_Init(AI_FEATURE_EXTRACTOR_VERSION);
    (void)PrototypeHead_Init(AI_FEATURE_EXTRACTOR_VERSION);
  }
}

/* USER CODE END Application */

