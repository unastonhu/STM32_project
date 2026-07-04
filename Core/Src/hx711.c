#include "hx711.h"
#include "freertos.h"
#include "cmsis_os2.h" 

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
// 1. 底层读取函数 (带超时逃生机制)
// ==========================================
int32_t HX711_ReadRaw(void)
{
    uint32_t count = 0;
    uint32_t timeout = 0;

    
    // 2. 把死等改成带超时的等待
    // 如果线断了，引脚可能一直拉不低，等一小会儿就直接返回错误码！
    while(HAL_GPIO_ReadPin(hx711_dout_port, hx711_dout_pin) == GPIO_PIN_SET)
    {
        timeout++;
        if(timeout > 50000) { // 这个数字根据你的主频微调，大约等几毫秒
            return 0xFFFFFFFF; // 返回一个极端的错误标识
        }
    }

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

    HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_SET);
    HX711_Delay();
    count = count ^ 0x800000; 
    HAL_GPIO_WritePin(hx711_sck_port, hx711_sck_pin, GPIO_PIN_RESET);
    HX711_Delay();

    return (int32_t)count;
}

// ==========================================
// 2. 去皮函数
// ==========================================
void HX711_Tare(void)
{
    int32_t sum = 0;
    for(int i = 0; i < 10; i++)
    {
        int32_t raw = HX711_ReadRaw();
        
        // 拦截底层刚加的超时故障码！
        // 如果底层超时返回了 0xFFFFFFFF (强转为 int32_t 就是 -1)，说明开机就没接线
        // 此时去皮没有任何意义，直接退出，防止算出错误的皮重！
        if (raw == -1) return; 

        sum += raw;
        
        // 每次读取之间休息一下，让 HX711 芯片准备好下一次数据
        osDelay(20); 
    }
    hx711_offset = sum / 10; // 算出并保存真正的皮重
}


// ==========================================
// 3. 初始化函数 (必须排在 Tare 后面)
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
    uint32_t raw_val = HX711_ReadRaw();
    
    // 拦截底层故障码，向上级报警
    if(raw_val == 0xFFFFFFFF) {
        return -999.0f; 
    }

    float weight = (float)(raw_val - hx711_offset) / hx711_scale;
    if(weight < 0.0f) weight = 0.0f; 
    return weight;
}
