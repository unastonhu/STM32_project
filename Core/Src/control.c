#include "control.h"
#include "FreeRTOS.h"
#include "task.h"

// ===========================================================================
// [私有函数] 臭氧安全死锁监控器 (禁止外部直接调用)
// ===========================================================================
static void Ozone_Safety_Monitor(void)
{
    uint32_t current_tick = xTaskGetTickCount(); 

    if (sysData.ozone_is_locked == 1) {
        // 🔴 状态：死锁中
        sysData.relays.ozone = 0; // 绝对武力压制，强制写 0
        
        // 检查是否度过了强制冷却期
        if ((current_tick - sysData.ozone_lock_tick) >= OZONE_COOL_TIME) {
            sysData.ozone_is_locked = 0; // 冷却完毕，解开死锁
        }
    } 
    else {
        // 🟢 状态：正常
        if (sysData.relays.ozone == 1) {
            if (sysData.ozone_start_tick == 0) {
                // 刚被开启，记录此刻时间
                sysData.ozone_start_tick = current_tick;
            } 
            else if ((current_tick - sysData.ozone_start_tick) >= OZONE_MAX_ON_TIME) {
                // 💥 触发报警：运行时间超过最大限制！
                sysData.relays.ozone = 0;        // 强制关停
                sysData.ozone_is_locked = 1;     // 挂上死锁标志
                sysData.ozone_lock_tick = current_tick; // 记录死锁开始时间
                sysData.ozone_start_tick = 0;    // 运行时间清零
            }
        } 
        else {
            // 被安全关闭，清零运行时间
            sysData.ozone_start_tick = 0; 
        }
    }
}

// ===========================================================================
// [私有函数] 14路执行器硬件抽象驱动层 (禁止外部直接调用)
// ===========================================================================
static void Relay_Hardware_Sync(void)
{
    // --- 1. 小电流控制组：环境净化 (PB1, PB2) ---
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, sysData.relays.ozone ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, sysData.relays.uv_lamp ? GPIO_PIN_RESET : GPIO_PIN_SET);

    // --- 2. 小电流控制组：风扇网络 (PA2 ~ PA5) ---
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, sysData.relays.cool_fans[0] ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, sysData.relays.cool_fans[1] ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, sysData.relays.duct_fans[0] ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, sysData.relays.duct_fans[1] ? GPIO_PIN_RESET : GPIO_PIN_SET);

    // --- 3. 大功率控制组：半导体制冷核心 (PE7 ~ PE10) ---
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7,  sysData.relays.coolers[0] ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8,  sysData.relays.coolers[1] ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9,  sysData.relays.coolers[2] ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, sysData.relays.coolers[3] ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

// ===========================================================================
// [公开 API] 综合硬件控制大循环
// ===========================================================================
void Control_Update_Routine(void)
{
    // 1. 先执行逻辑防御，过滤非法/超时指令
    Ozone_Safety_Monitor();
    
    // 2. 将过滤后的纯净、安全状态，一键映射到物理硬件
    Relay_Hardware_Sync();
}

// ===========================================================================
// [公开 API] 高级外设数量分配中心
// ===========================================================================

/**
  * @brief  智能分配制冷模块 (TEC) 的开启数量
  * @param  count: 期望开启的个数 (0 ~ 4)
  */
void Control_Set_Coolers(uint8_t count)
{
    // 防呆保护：最多只有 4 个
    if (count > 4) count = 4;
    
    for (int i = 0; i < 4; i++) {
        // 如果当前索引小于期望开启的数量，就置 1；否则置 0
        sysData.relays.coolers[i] = (i < count) ? 1 : 0;
    }
}

/**
  * @brief  智能分配散热大风扇的开启数量
  * @param  count: 期望开启的个数 (0, 2, 4)。由于是双绞线并联，1个继电器控制2个。
  */
void Control_Set_CoolFans(uint8_t count)
{
    if (count >= 4) {
        // 请求开 4 个，两个通道全开
        sysData.relays.cool_fans[0] = 1;
        sysData.relays.cool_fans[1] = 1;
    } 
    else if (count >= 2) {
        // 请求开 2 个，只开通道 A
        sysData.relays.cool_fans[0] = 1;
        sysData.relays.cool_fans[1] = 0;
    } 
    else {
        // 请求开 0 个，全关
        sysData.relays.cool_fans[0] = 0;
        sysData.relays.cool_fans[1] = 0;
    }
}

/**
  * @brief  智能分配风道搅动小风扇的开启数量
  * @param  count: 期望开启的个数 (0, 2, 4)。
  */
void Control_Set_DuctFans(uint8_t count)
{
    if (count >= 4) {
        sysData.relays.duct_fans[0] = 1;
        sysData.relays.duct_fans[1] = 1;
    } 
    else if (count >= 2) {
        sysData.relays.duct_fans[0] = 1;
        sysData.relays.duct_fans[1] = 0;
    } 
    else {
        sysData.relays.duct_fans[0] = 0;
        sysData.relays.duct_fans[1] = 0;
    }
}

