/*
*
*@file    fridge_weight_engine.c
*
*@brief   智能冰箱重量滤波、阶跃捕获与虚拟物品匹配实现
*/

#include "fridge_weight_engine.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/*
*
*@brief 初始化重量引擎
*/
void FridgeWeight_Init(FridgeWeightEngine_t *engine)
{
memset(engine, 0, sizeof(FridgeWeightEngine_t));
}

/*
*
*@brief 中值+滑动平均复合滤波器 (消除压缩机震动与奇异值)
*/
float FridgeWeight_UpdateFilter(FridgeWeightEngine_t *engine, float raw_weight)
{
// 如果底层读取报错 (-999.0f)，直接保持上一次的滤波值
if (raw_weight < -900.0f) {
return engine->current_filtered_w;
}

// 1. 填入滑动窗口
engine->filter_buf[engine->filter_idx] = raw_weight;
engine->filter_idx = (engine->filter_idx + 1) % FILTER_WINDOW_SIZE;

// 2. 复制窗口数据进行冒泡排序 (求中值)
float sorted[FILTER_WINDOW_SIZE];
memcpy(sorted, engine->filter_buf, sizeof(sorted));

for (int i = 0; i < FILTER_WINDOW_SIZE - 1; i++) {
for (int j = 0; j < FILTER_WINDOW_SIZE - i - 1; j++) {
if (sorted[j] > sorted[j + 1]) {
float temp = sorted[j];
sorted[j] = sorted[j + 1];
sorted[j + 1] = temp;
}
}
}

// 3. 去掉最大值和最小值，其余求平均
float sum = 0.0f;
int count = 0;
for (int i = 1; i < FILTER_WINDOW_SIZE - 1; i++) {
sum += sorted[i];
count++;
}

engine->current_filtered_w = (count > 0) ? (sum / (float)count) : raw_weight;
return engine->current_filtered_w;
}

/*
*
*@brief 拿出物品时的“衰减预估匹配”算法
*/
int FridgeWeight_MatchAndRemoveItem(FridgeWeightEngine_t *engine, float step_weight, uint32_t now_sec)
{
int best_index = -1;
float min_error = 99999.0f;

for (int i = 0; i < MAX_VIRTUAL_ITEMS; i++) {
if (!engine->items[i].is_active) continue;

 // 1. 计算存放时长 (天)
 float elapsed_days = (float)(now_sec - engine->items[i].put_in_time) / 86400.0f;
 if (elapsed_days < 0.0f) elapsed_days = 0.0f;

 // 2. 计算当前理论期望重量 (带有基础失水补偿)
 float expected_w = engine->items[i].initial_weight * (1.0f - BASE_DAILY_LOSS * elapsed_days);
 if (expected_w < 1.0f) expected_w = 1.0f;

 // 3. 对比负阶跃绝对值与预期重量的偏差
 float error = fabsf(step_weight - expected_w);

 if (error < min_error) {
     min_error = error;
     best_index = i;
 }


}

// 4. 误差在许用门限内才执行移除
if (best_index != -1 && min_error < MATCH_TOLERANCE_G) {
engine->items[best_index].is_active = 0;
if (engine->active_count > 0) engine->active_count--;
return best_index;
}

return -1; // 未匹配到已知物品
}

/*
*
*@brief 开门期间 (sysData.ir.status == 0) 的动作分割与阶跃捕获
*/
void FridgeWeight_ProcessDoorOpen(FridgeWeightEngine_t *engine, uint8_t door_is_open, uint32_t now_sec)
{
if (!door_is_open) return;

float cur_w = engine->current_filtered_w;
float diff = cur_w - engine->last_stable_w;

// 1. 检查物理重量是否产生跳变
if (fabsf(diff) > STEP_DEADZONE_G) {
engine->is_moving = 1;
engine->stable_counter = 0; // 重置稳定计时
} else {
// 重量保持平稳
if (engine->is_moving) {
engine->stable_counter++;

     // 连续 5 次采样稳定 (比如约 500ms)，确认手已离开，完成一次动作捕捉！
     if (engine->stable_counter >= 5) {
         engine->is_moving = 0;
         float step_value = cur_w - engine->last_stable_w;

         if (step_value > STEP_DEADZONE_G) {
             // 正阶跃：放入新物品
             for (int i = 0; i < MAX_VIRTUAL_ITEMS; i++) {
                 if (!engine->items[i].is_active) {
                     engine->items[i].initial_weight = step_value;
                     engine->items[i].put_in_time = now_sec;
                     engine->items[i].is_active = 1;
                     engine->active_count++;
                     break;
                 }
             }
         } else if (step_value < -STEP_DEADZONE_G) {
             // 负阶跃：拿走物品，触发衰减匹配
             FridgeWeight_MatchAndRemoveItem(engine, fabsf(step_value), now_sec);
         }

         // 刷新稳定锚点
         engine->last_stable_w = cur_w;
     }
 } else {
     engine->last_stable_w = cur_w;
 }


}
}

/*
*
*@brief 关门自愈校准 (根据关门后真正的物理稳定总重，归一化修正虚拟列表)
*/
void FridgeWeight_SelfHeal(FridgeWeightEngine_t *engine, float actual_stable_weight)
{
engine->base_anchor_weight = actual_stable_weight;
engine->last_stable_w = actual_stable_weight;

if (engine->active_count == 0) return;

// 计算虚拟列表总重
float array_sum = 0.0f;
for (int i = 0; i < MAX_VIRTUAL_ITEMS; i++) {
if (engine->items[i].is_active) {
array_sum += engine->items[i].initial_weight;
}
}

if (array_sum < 1.0f) return;

// 计算校准比例系数
float gamma = actual_stable_weight / array_sum;

// 若偏差超过 5%，执行按比例等比缩放自愈
if (fabsf(gamma - 1.0f) > 0.05f) {
for (int i = 0; i < MAX_VIRTUAL_ITEMS; i++) {
if (engine->items[i].is_active) {
engine->items[i].initial_weight *= gamma;
}
}
}
}