#include "ds18b20.h"
//#include <assert.h>

#define DQ_OUT(x) HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, x ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define DQ_IN() HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8)

//------------------------------------
static void delay_us(uint32_t us){
  uint32_t delay = (SystemCoreClock / 1000000 * us)/ 4;
  while(delay--){
    __NOP();
  }
}

//--------------------------------------
// ---------------- 内部读写函数 ----------------
// 向传感器写 1 个字节的数据
static void DS18B20_WriteByte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        DQ_OUT(0);         // 拉低总线
        delay_us(2);
        
        if (data & 0x01) {
            DQ_OUT(1);     // 写 1：释放总线
        } else {
            DQ_OUT(0);     // 写 0：保持低电平
        }
        delay_us(60);      
        
        DQ_OUT(1);         // 释放总线
        delay_us(2);
        data >>= 1;        
    }
}

// 从传感器读 1 个字节的数据
static uint8_t DS18B20_ReadByte(void)
{
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        data >>= 1;
        
        DQ_OUT(0);         // 拉低总线
        delay_us(2);
        DQ_OUT(1);         // 释放总线
        delay_us(12);      
        
        if (DQ_IN()) 
        {     // 读取总线状态
            data |= 0x80;  
        }
        delay_us(50);      
    }
    return data;
}

// ---------------- 暴露给外部的接口函数 ----------------

// 复位传感器并检测是否存在
uint8_t DS18B20_Init(void)
{
    uint8_t presence = 0;
    DQ_OUT(0);      // 单片机拉低总线
    delay_us(600);  // 保持低电平 480~700us
    
    DQ_OUT(1);      // 释放总线
    delay_us(60);   // 等待 15~60us
    
    presence = DQ_IN(); // 0表示成功检测到传感器，非0表示失败
    delay_us(400);  
    
    return presence; 
}

// 获取当前温度
float DS18B20_GetTemp(void)
{
    uint8_t LSB, MSB;
    uint16_t temp_raw;
    
    if (DS18B20_Init() != 0) return -100.0f; // 检测不到传感器返回错误值

    DS18B20_WriteByte(0xCC); // 跳过 ROM
    DS18B20_WriteByte(0x44); // 开始温度转换

    HAL_Delay(750);          // DS18B20 转换时间

    DS18B20_Init();          
    DS18B20_WriteByte(0xCC); 
    DS18B20_WriteByte(0xBE); // 读取暂存器

    LSB = DS18B20_ReadByte(); // 低 8 位
    MSB = DS18B20_ReadByte(); // 高 8 位

    temp_raw = (MSB << 8) | LSB;      
    return temp_raw * 0.0625f;        
}
