#include "hx711.h"
#include "freertos.h"
#include "cmsis_os.h"

// 内部记录保存的硬件引脚，供后续读取使用
static GPIO_TypeDef *hx711_sck_port = NULL;
static uint16_t      hx711_sck_pin  = 0;
static GPIO_TypeDef *hx711_dout_port = NULL;
static uint16_t      hx711_dout_pin = 0;

static int32_t hx711_offset = 0; // 皮重（零点偏移量）
static float   hx711_scale  = 418.0f; // 💡 初始校准系数（先填个大概，后面带你校准）

// 软件微秒级粗略延时，防止 STM32 跑得太快让 HX711 反应不过来 
static void HX711_Delay(void)
{
    uint32_t i = 15;
    while(i--);
}

void HX711_Init(GPIO_TypeDef *sck_port, uint16_t sck_pin, GPIO_TypeDef *dout_port, uint16_t dout_pin)
{
    hx711_sck_port  = sck_port;
    hx711_sck_pin   = sck_pin;
    hx711_dout_port = dout_port;
    hx711_dout_pin  = dout_pin;

    // 确保时钟线初始为低电平 [cite: 350]
    HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_RESET);
    
    // 上电自动去皮清零
    HX711_Tare();
}

int32_t HX711_ReadRaw(void)
{
    uint32_t count = 0;

    // 1. 严格死等 DOUT 变低电平（表示 HX711 已经转换完成，数据准备就绪）
    // 如果硬件断线，程序会卡在这里。工业级项目这应该加超时检测，咱们实验先这样最直观

    while(HAL_GPIO_ReadPin(hx711_dout_port, hx711_dout_pin) == GPIO_PIN_SET);

    // 2. 循环 24 次，通过时钟脉冲一位一位地把 24 位数据读出来 
    for(int i = 0; i < 24; i++)
    {
        HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_SET); // SCK 拉高 
        HX711_Delay();
        count = count << 1; // 变量左移一位 [cite: 625]
        HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_RESET); // SCK 拉低 
        HX711_Delay();
        
        if(HAL_GPIO_ReadPin(hx711_dout_port, hx711_dout_pin) == GPIO_PIN_SET)
        {
            count++; // 如果读到高电平，对应位置 1 [cite: 627]
        }
    }

    // 3. 第 25 个脉冲：告诉 HX711 下一次读取使用通道 A，放大增益 128 倍 [cite: 322, 323]
    HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_SET);
    HX711_Delay();
    count = count ^ 0x800000; // 官方手册要求的二进制补码转换逻辑 [cite: 630]
    HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_RESET);
    HX711_Delay();

    return (int32_t)count;
}

void HX711_Tare(void)
{
    int32_t sum = 0;
    // 连续读取 10 次取平均值作为零点偏移量
    for(int i = 0; i < 10; i++)
    {
        sum += HX711_ReadRaw();
        osDelay(20); // 给系统腾出喘息时间
    }
    hx711_offset = sum / 10;
}

float HX711_GetWeight(void)
{
    int32_t raw = HX711_ReadRaw();
    // 实际重量 = (当前读数值 - 零点皮重) / 比例系数
    float weight = (float)(raw - hx711_offset) / hx711_scale;
    
    if(weight < 0.0f) weight = 0.0f; // 过滤轻微的负数抖动
    return weight;
}
