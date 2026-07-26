/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "cmsis_os.h"
#include "dma.h"
#include "fatfs.h"
#include "i2c.h"
#include "sdio.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ds18b20.h"
#include "hcsr04.h"
#include "hx711.h"
#include "dht11.h"

#include "system_data.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

static uint8_t k230_rx_byte;
static char k230_rx_buf[128];
static volatile uint16_t k230_rx_len;
static volatile uint8_t k230_rx_ready;
static uint32_t k230_last_valid_tick;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/*
 * USART2 中断只做定长内存操作。
 * strstr/sscanf 等字符串解析放到普通任务执行，避免中断占用时间过长。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        /*
         * 上一帧尚未被任务处理时丢弃新字节，确保任务解析期间缓冲区
         * 不会被中断改写。K230 会周期上报，丢一帧优于解析半帧。
         */
        if (k230_rx_ready == 0U) {
            if (k230_rx_byte == '\n') {
                if (k230_rx_len > 0U) {
                    k230_rx_buf[k230_rx_len] = '\0';
                    k230_rx_ready = 1U;
                }
                k230_rx_len = 0U;
            }
            else if (k230_rx_byte != '\r') {
                if (k230_rx_len < (sizeof(k230_rx_buf) - 1U)) {
                    k230_rx_buf[k230_rx_len++] = (char)k230_rx_byte;
                } else {
                    /* 超长帧作废，等待下一行重新同步。 */
                    k230_rx_len = 0U;
                }
            }
        }

        (void)HAL_UART_Receive_IT(&huart2, &k230_rx_byte, 1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        /*
         * 杜邦线接触抖动可能产生帧错/溢出。丢弃当前半帧并重新挂接
         * 单字节接收，避免一次串口噪声让 K230 链路永久停止。
         */
        k230_rx_len = 0U;
        k230_rx_ready = 0U;
        (void)HAL_UART_Receive_IT(&huart2, &k230_rx_byte, 1U);
    }
}

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint16_t K230_ClampCount(int value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 65535) {
        return 65535U;
    }
    return (uint16_t)value;
}

void K230_ProcessPendingFrame(void)
{
    uint32_t now = HAL_GetTick();

    if (k230_rx_ready != 0U) {
        int apple = 0;
        int banana = 0;
        int orange = 0;
        char *apple_ptr = strstr(k230_rx_buf, "\"apple\":");
        char *banana_ptr = strstr(k230_rx_buf, "\"banana\":");
        char *orange_ptr = strstr(k230_rx_buf, "\"orange\":");
        bool command_ok =
            strstr(k230_rx_buf, "\"cmd\":\"AI_FRUIT\"") != NULL ||
            strstr(k230_rx_buf, "\"cmd\": \"AI_FRUIT\"") != NULL;
        bool values_ok =
            apple_ptr != NULL &&
            banana_ptr != NULL &&
            orange_ptr != NULL &&
            sscanf(apple_ptr + 8, "%d", &apple) == 1 &&
            sscanf(banana_ptr + 9, "%d", &banana) == 1 &&
            sscanf(orange_ptr + 9, "%d", &orange) == 1;

        /*
         * status 最后写入，相当于发布标志。只有命令和三个计数全部合法，
         * 才用这一帧覆盖上一份视觉结果。
         */
        if (command_ok && values_ok) {
            taskENTER_CRITICAL();
            sysData.k230.apple = K230_ClampCount(apple);
            sysData.k230.banana = K230_ClampCount(banana);
            sysData.k230.orange = K230_ClampCount(orange);
            sysData.k230.status = 1;
            k230_last_valid_tick = now;
            taskEXIT_CRITICAL();
        }

        taskENTER_CRITICAL();
        k230_rx_ready = 0U;
        taskEXIT_CRITICAL();
    }

    /* 连续 3 秒没有收到完整合法帧时标记离线，避免永久显示旧数据。 */
    if (sysData.k230.status == 1 &&
        (uint32_t)(now - k230_last_valid_tick) >= 3000U) {
        sysData.k230.status = 0;
    }
}

// printf 重定向代码 (把标准输出定向到 huart1)
#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
    // 将数据通过 USART1 发送给电脑
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_TIM4_Init();
  MX_I2C1_Init();
  MX_SPI2_Init();
  MX_SDIO_SD_Init();
  MX_FATFS_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

   /*
    * 两片 W25Q64 的 CS 都是低有效，再次拉高作为 Cube 重新生成后的保护。
    * CubeMX 中仍需把 W25_02_CS(PC5) 的 GPIO output level 配为 High。
    */
   HAL_GPIO_WritePin(W25_01_CS_GPIO_Port, W25_01_CS_Pin, GPIO_PIN_SET);
   HAL_GPIO_WritePin(W25_02_CS_GPIO_Port, W25_02_CS_Pin, GPIO_PIN_SET);
   HAL_UART_Receive_IT(&huart2, &k230_rx_byte, 1);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */


// 定时器捕获回调
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    HCSR04_CaptureCallback(htim);
}





/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM7 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM7)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  //  在这里咱们超声波的溢出回调代码！
  HCSR04_TmrOverflowCallback(htim);

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
