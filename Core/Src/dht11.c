#include "dht11.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

static GPIO_TypeDef *dht11_port = NULL;
static uint16_t      dht11_pin  = 0;

// 微秒级软延时
static void DHT11_Delay_Us(volatile uint32_t us) {
    volatile uint32_t delay = us * 30; // 适配STM32F4的粗略延时参数
    while(delay--);
}

// 动态：将引脚切换为推挽输出
static void DHT11_Mode_Out(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = dht11_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(dht11_port, &GPIO_InitStruct);
}

// 动态：将引脚切换为浮空输入
static void DHT11_Mode_In(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = dht11_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(dht11_port, &GPIO_InitStruct);
}

void DHT11_Init(GPIO_TypeDef *gpio_port, uint16_t gpio_pin) {
    dht11_port = gpio_port;
    dht11_pin  = gpio_pin;
    
    DHT11_Mode_Out();
    HAL_GPIO_WritePin(dht11_port, dht11_pin, GPIO_PIN_SET);
    // 官方手册硬性要求：上电后必须等待 1 秒越过不稳定状态
    osDelay(1000); 
}

int8_t DHT11_Read_Data(uint8_t *humidity, uint8_t *temperature) {
    uint8_t buf[5] = {0};
    int8_t error_code = 0;
    uint32_t timeout = 0;

    taskENTER_CRITICAL(); // 🔒 锁中断，保护极其脆弱的微秒时序

    // 1. 主机发起始信号：拉低20ms，再拉高30us
    DHT11_Mode_Out();
    HAL_GPIO_WritePin(dht11_port, dht11_pin, GPIO_PIN_RESET);
    DHT11_Delay_Us(20000); 
    HAL_GPIO_WritePin(dht11_port, dht11_pin, GPIO_PIN_SET);
    DHT11_Delay_Us(30);    

    // 2. 切换为输入模式，听传感器响应
    DHT11_Mode_In();       

    if(HAL_GPIO_ReadPin(dht11_port, dht11_pin) == GPIO_PIN_SET) {
        error_code = -1; goto end; // ❌ 错误-1：传感器无响应
    }

    // 等待传感器80us低电平结束
    timeout = 0;
    while(HAL_GPIO_ReadPin(dht11_port, dht11_pin) == GPIO_PIN_RESET) {
        if(timeout++ > 10000) { error_code = -2; goto end; } // ❌ 错误-2
    }
    
    // 等待传感器80us高电平结束
    timeout = 0;
    while(HAL_GPIO_ReadPin(dht11_port, dht11_pin) == GPIO_PIN_SET) {
        if(timeout++ > 10000) { error_code = -3; goto end; } // ❌ 错误-3
    }
    
    // 3. 开始接收40位数据
    for(int i = 0; i < 5; i++) {
        for(int j = 0; j < 8; j++) {
            timeout = 0;
            // 等待50us低电平起步结束
            while(HAL_GPIO_ReadPin(dht11_port, dht11_pin) == GPIO_PIN_RESET){
                 if(timeout++ > 10000) { error_code = -4; goto end; } // ❌ 错误-4
            }
            
            // 延时40us，看高电平是短(0)还是长(1)
            DHT11_Delay_Us(40); 
            
            buf[i] <<= 1;
            if(HAL_GPIO_ReadPin(dht11_port, dht11_pin) == GPIO_PIN_SET) {
                buf[i] |= 1;
                timeout = 0;
                // 如果是1，等高电平彻底结束
                while(HAL_GPIO_ReadPin(dht11_port, dht11_pin) == GPIO_PIN_SET){
                     if(timeout++ > 10000) { error_code = -5; goto end; } // ❌ 错误-5
                }
            }
        }
    }
    
    // 4. 校验数据
    if((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) == buf[4]) {
        *humidity    = buf[0];
        *temperature = buf[2];
        error_code = 1; // ✅ 成功
    } else {
        error_code = -6; // ❌ 错误-6：数据错乱，校验失败
    }

end:
    taskEXIT_CRITICAL(); // 🔓 开中断
    DHT11_Mode_Out();    // 强制切回输出模式并拉高总线，恢复空闲状态
    HAL_GPIO_WritePin(dht11_port, dht11_pin, GPIO_PIN_SET);
    return error_code;
}
