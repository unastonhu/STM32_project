#ifndef __CONTROL_H
#define __CONTROL_H

#include "main.h"
#include "system_data.h"

// ==========================================
// 反应舱核心控制参数 (时间单位: 毫秒)
// ==========================================
#define OZONE_MAX_ON_TIME  (3 * 60 * 1000)   // 臭氧最长开启时间：3分钟
#define OZONE_COOL_TIME    (30 * 60 * 1000)  // 强制死锁通风时间：30分钟

// ==========================================
// 暴露给外部任务的 API 接口
// ==========================================

// 硬件控制系统大循环 (需在 RTOS 中周期调用)
void Control_Update_Routine(void);


// 🌟 新增：高级外设数量分配 API
void Control_Set_Coolers(uint8_t count);   // 参数: 0, 1, 2, 3, 4
void Control_Set_CoolFans(uint8_t count);  // 参数: 0, 2, 4 (2个并联)
void Control_Set_DuctFans(uint8_t count);  // 参数: 0, 2, 4 (2个并联)

// 暴露给外部任务的 API 接口
// ==========================================

// 🌟 新增：电子鼻初始化与滴答引擎
void Control_ENose_Init(void);
void Control_ENose_Tick(void);

// 硬件控制系统大循环 (需在 RTOS 中周期调用)
void Control_Update_Routine(void);

#endif /* __CONTROL_H */