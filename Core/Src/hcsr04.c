#include "hcsr04.h"
#include "tim.h"

// 内部记录传入的定时器信息
static TIM_HandleTypeDef *hcsr04_htim = NULL;
static uint32_t           hcsr04_channel = 0;

// 内部状态机变量
static uint8_t  capture_Stage = 0;    
static uint32_t edge_RiseVal = 0;     
static uint32_t edge_FallVal = 0;     
static uint32_t timer_OverflowCnt = 0;

/**
 * @brief 初始化超声波测距定时器
 */
void HCSR04_Init(TIM_HandleTypeDef *htim, uint32_t channel)
{
    // 保存定时器句柄和通道，供后续中断使用
    hcsr04_htim = htim;
    hcsr04_channel = channel;
    
    // 启动定时器的输入捕获中断和溢出中断
    HAL_TIM_IC_Start_IT(hcsr04_htim, hcsr04_channel);
    __HAL_TIM_ENABLE_IT(hcsr04_htim, TIM_IT_UPDATE);
}

/**
 * @brief 触发一次超声波测距信号 (带参数版)
 */
void HCSR04_StartTrigger(GPIO_TypeDef *TRIG_PORT, uint16_t TRIG_PIN)
{
    if(capture_Stage == 0) // 确保上一次测量已经处理完毕
    {
        // 按照手册要求：给 Trig 一个大于 10us 的高电平触发信号
        HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_SET);
        for(volatile int i = 0; i < 200; i++); // 粗略延时，绝对够 10us
        HAL_GPIO_WritePin(TRIG_PORT, TRIG_PIN, GPIO_PIN_RESET);
    }
}

/**
 * @brief 输入捕获中断回调核心函数
 */
void HCSR04_CaptureCallback(TIM_HandleTypeDef *htim)
{
    // 检查是不是咱们超声波绑定的那个定时器
    if(hcsr04_htim != NULL && htim->Instance == hcsr04_htim->Instance)
    {
        if(capture_Stage == 0) // 抓到了上升沿
        {
            edge_RiseVal = HAL_TIM_ReadCapturedValue(htim, hcsr04_channel);
            timer_OverflowCnt = 0;
            // 切换为捕获下降沿
            __HAL_TIM_SET_CAPTUREPOLARITY(htim, hcsr04_channel, TIM_ICPOLARITY_FALLING);
            capture_Stage = 1;
        }
        else if(capture_Stage == 1) // 抓到了下降沿
        {
            edge_FallVal = HAL_TIM_ReadCapturedValue(htim, hcsr04_channel);
            // 恢复为捕获上升沿
            __HAL_TIM_SET_CAPTUREPOLARITY(htim, hcsr04_channel, TIM_ICPOLARITY_RISING);
            capture_Stage = 2; // 标记单次测量数据已锁定
        }
    }
}

/**
 * @brief 定时器溢出中断回调
 */
void HCSR04_TmrOverflowCallback(TIM_HandleTypeDef *htim)
{
    if(hcsr04_htim != NULL && htim->Instance == hcsr04_htim->Instance)
    {
        if(capture_Stage == 1) // 只有正在高电平时溢出才算数
        {
            timer_OverflowCnt++;
        }
    }
}

/**
 * @brief 计算并获取最终物理距离
 */
float HCSR04_GetDistance(void)
{
    uint32_t total_Ticks = 0;
    float distance = 0.0f;
    
    if(capture_Stage == 2)
    {
        total_Ticks = (edge_FallVal + timer_OverflowCnt * 65536) - edge_RiseVal;
        
        // 假设定时器配置为 1MHz (1us/tick)，声音往返换算系数为 0.017
        distance = (float)total_Ticks * 0.017f; 
        
        capture_Stage = 0; // 解锁状态机
        return distance;
    }
    return -1.0f; 
}
