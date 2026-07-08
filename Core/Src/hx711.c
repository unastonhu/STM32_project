#include "hx711.h"
#include "freertos.h"
#include "cmsis_os2.h" 
#include "task.h"

// 内部记录保存的硬件引脚，供后续读取使用
static GPIO_TypeDef *hx711_sck_port = NULL;
static uint16_t      hx711_sck_pin  = 0;
static GPIO_TypeDef *hx711_dout_port = NULL;
static uint16_t      hx711_dout_pin = 0;

static int32_t hx711_offset = 0; // 皮重（零点偏移量）
static float   hx711_scale  = 418.0f; // 初始校准系数

// 软件微秒级粗略延时
static void HX711_Delay(void)
{
    uint32_t i = 15;
    while(i--);
}

// ==========================================
// 1. 底层读取函数 (带超时逃生机制 + 临界区保护)
// ==========================================
int32_t HX711_ReadRaw(void)
{
    uint32_t count = 0;
    uint32_t timeout = 0;

    // 1. 等待 DOUT 拉低 (表示数据准备好)
    while(HAL_GPIO_ReadPin(hx711_dout_port, hx711_dout_pin) == GPIO_PIN_SET)
    {
        timeout++;
        // 增加超时容忍度，防止刚上电传感器还没准备好
        if(timeout > 200000) { 
            return -1; // 🌟 统一返回带符号的负数 -1 作为绝对故障码
        }
    }

    // 🌟 2. 核心保护：进入临界区，禁止被 FreeRTOS 任务或中断打断！
    taskENTER_CRITICAL();

    for(int i = 0; i < 24; i++)
    {
        HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_SET); 
        HX711_Delay();
        count = count << 1; 
        HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_RESET); 
        HX711_Delay();
        
        if(HAL_GPIO_ReadPin(hx711_dout_port, hx711_dout_pin) == GPIO_PIN_SET)
        {
            count++; 
        }
    }

    // 第 25 个脉冲，设置下次增益为 128
    HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_SET);
    HX711_Delay();
    HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_RESET);
    HX711_Delay();

    // 🌟 3. 读取完毕，火速退出临界区，恢复系统调度
    taskEXIT_CRITICAL();

    // 🌟 4. 标准的 24位 补码转 32位 有符号数逻辑
    if (count & 0x800000) {
        count |= 0xFF000000;
    }

    return (int32_t)count;
}

// ==========================================
// 2. 去皮函数
// ==========================================
void HX711_Tare(void)
{
    int32_t sum = 0;
    int valid_count = 0;

    for(int i = 0; i < 10; i++)
    {
        int32_t raw = HX711_ReadRaw();
        
        // 🌟 拦截底层刚加的超时故障码 -1
        if (raw == -1) {
            osDelay(10);
            continue; // 如果这次超时，跳过，不要污染总和
        }

        sum += raw;
        valid_count++;
        osDelay(20); 
    }
    
    // 只有读到了有效数据，才计算皮重
    if (valid_count > 0) {
        hx711_offset = sum / valid_count; 
    }
}

// ==========================================
// 3. 初始化函数
// ==========================================
void HX711_Init(GPIO_TypeDef *sck_port, uint16_t sck_pin, GPIO_TypeDef *dout_port, uint16_t dout_pin)
{
    hx711_sck_port  = sck_port;
    hx711_sck_pin   = sck_pin;
    hx711_dout_port = dout_port;
    hx711_dout_pin  = dout_pin;

    HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_RESET);
    
    // 上电自动去皮清零
    HX711_Tare();
}

// ==========================================
// 4. 获取重量函数
// ==========================================
float HX711_GetWeight(void)
{
    int32_t raw_val = HX711_ReadRaw();
    
    // 🌟 拦截底层的 -1 故障码
    if(raw_val == -1) {
        return -999.0f; 
    }

    float weight = (float)(raw_val - hx711_offset) / hx711_scale;
    
    // 重量在 0 附近抖动时，强行清零
    if(weight < 0.5f && weight > -0.5f) weight = 0.0f; 
    if(weight < 0.0f) weight = 0.0f; 

    return weight;
}
