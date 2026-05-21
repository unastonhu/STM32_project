#include "mq_sensor.h"
#include "adc.h" // 引入 ADC 句柄定义
#include "dma.h" // 引入 DMA 句柄定义
#include "usart.h" // 引入 UART 句柄定义
#include "math.h" // 引入数学库 

// 外部引用 CubeMX 自动生成的 ADC1 句柄
extern ADC_HandleTypeDef hadc1;

// 【魔法核心】定义一个容量为 4 的数组
// 一旦启动 DMA，单片机硬件就会自动、悄悄地把 4 个引脚的电压数据实时覆盖到这个数组里！
uint16_t adc_buffer[4];


// 1. 定义你模块上的负载电阻 RL (根据你买的常规模块，通常是 1000 欧姆)
#define RL_MQ3    1000.0f
#define RL_MQ135  1000.0f

// 2. 定义绝对干净空气中的传感器电阻 R0 (⚠️ 极其重要：你需要根据你的板子实际修改这个值)
#define R0_MQ3    4500.0f  // 假设的干净空气阻值
#define R0_MQ135  4000.0f  // 假设的干净空气阻值

//=====================================================================================
// 初始化函数
void MQ_Init(void)
{
    // 启动 ADC 的 DMA 连续转换
    // 参数1：ADC句柄
    // 参数2：要把数据搬到哪个数组里 (强转为 uint32_t 指针是 HAL 库的要求)
    // 参数3：一次搬运多少个数据 (我们有 4 个通道，所以写 4)
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, 4);
}

// 获取原始的数字量 (0~4095)
uint16_t MQ_Get_RawValue(uint8_t channel)
{
    if (channel > 3) return 0; // 防呆保护，防止数组越界
    return adc_buffer[channel];
}

// 获取换算后的真实电压值 (伏特)
float MQ_Get_Voltage(uint8_t channel)
{
    if (channel > 3) return 0.0f;

    // 数学换算：STM32 的 ADC 是 12 位的，最大值是 4095，对应 3.3V 电压
    // 所以：真实电压 = (测到的数字量 / 4095.0) * 3.3
    return (adc_buffer[channel] / 4095.0f) * 3.3f;
}


// ==========================================
float MQ3_Get_mgL(float voltage) 
{
    if(voltage <= 0.01f) return 0.0f; // 防呆保护
    // 算出当前电阻 Rs
    float Rs = RL_MQ3 * (5.0f - voltage) / voltage;
    // 算出比值 Rs/R0
    float ratio = Rs / R0_MQ3;
    // 代入 MQ-3 经验公式 (A=0.4, B=-1.431)
    return 0.4f * pow(ratio, -1.431f); 
}

// ==========================================
// MQ-135 综合空气质量/氨气计算 (单位: PPM)
// ==========================================
float MQ135_Get_PPM(float voltage) 
{
    if(voltage <= 0.01f) return 0.0f; // 防呆保护
    // 算出当前电阻 Rs
    float Rs = RL_MQ135 * (5.0f - voltage) / voltage;
    // 算出比值 Rs/R0
    float ratio = Rs / R0_MQ135;
    // 代入 MQ-135 经验公式 (A=116.6, B=-2.769)
    return 116.6f * pow(ratio, -2.769f); 
}
