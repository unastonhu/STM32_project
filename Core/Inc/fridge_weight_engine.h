/*
*
*@file    fridge_weight_engine.h
*
*@brief   智能冰箱重量中值滤波、阶跃捕获与虚拟物品阵列管理引擎
*/

#ifndef FRIDGE_WEIGHT_ENGINE_H
#define FRIDGE_WEIGHT_ENGINE_H

#include "main.h"

#define MAX_VIRTUAL_ITEMS   20      // 冰箱最大支持记录的虚拟主体数
#define FILTER_WINDOW_SIZE  7       // 中值滑动滤波窗口大小
#define STEP_DEADZONE_G     25.0f   // 判定为“拿放动作”的最小重量阶跃门限 (克)
#define MATCH_TOLERANCE_G   30.0f   // 负阶跃匹配的最大允许误差门限 (克)
#define MOTION_STABILITY_G   5.0f   // 相邻两拍变化低于此值才累计稳定时间 (克)
#define BASE_DAILY_LOSS     0.01f   // 默认基础失水率 (每天 1%)

/*
*
*@brief 虚拟物品结构体
*/
typedef struct {
float    initial_weight;  // 放入时刻的初始重量 (g)
uint32_t put_in_time;     // 放入时刻的时间戳 (秒)
uint8_t  is_active;       // 1: 在箱内, 0: 已被拿走
} VirtualItem_t;

/*
*
*@brief 重量引擎数据结构体
*/
typedef struct {
VirtualItem_t items[MAX_VIRTUAL_ITEMS]; // 虚拟物品阵列
uint8_t       active_count;             // 当前箱内有效物品数

float filter_buf[FILTER_WINDOW_SIZE];   // 滤波滑动窗口
uint8_t filter_idx;                     // 滤波索引
uint8_t filter_count;                   // 已填入的真实样本数，避免启动期把零值参与平均

float current_filtered_w;               // 滤波后的当前实时物理总重 (g)
float last_stable_w;                    // 动作分割用的上一次稳定重量 (g)
float last_sample_w;                    // 相邻拍稳定性判断，不等同于动作前锚点
uint8_t is_moving;                      // 0: 秤盘稳定, 1: 手或物体晃动中
uint32_t stable_counter;                // 连续稳定计数器

float base_anchor_weight;               // 关门稳定后的总重物理锚点 (g)
} FridgeWeightEngine_t;

// API 函数声明
void FridgeWeight_Init(FridgeWeightEngine_t *engine);
float FridgeWeight_UpdateFilter(FridgeWeightEngine_t *engine, float raw_weight);
void FridgeWeight_ProcessDoorOpen(FridgeWeightEngine_t *engine, uint8_t door_is_open, uint32_t now_sec);
int  FridgeWeight_MatchAndRemoveItem(FridgeWeightEngine_t *engine, float step_weight, uint32_t now_sec);
void FridgeWeight_SelfHeal(FridgeWeightEngine_t *engine, float actual_stable_weight);

#endif /* FRIDGE_WEIGHT_ENGINE_H */
