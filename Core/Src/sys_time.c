/*
*@file    sys_time.c
*
*@brief   基于 USB 外接同步的轻量级 UNIX 时间戳与时区管理模块实现
*/

#include "sys_time.h"
#include "main.h" // 确保能访问到 HAL 库
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 全局时间实体
SysTime_t g_sys_time = {0};

void SysTime_Init(void)
{
memset(&g_sys_time, 0, sizeof(SysTime_t));
g_sys_time.tz_offset_hours = 8; // 默认东八区 (UTC+8)
g_sys_time.is_valid = false;
}

void SysTime_SetUnixTime(uint32_t unix_ts, int8_t tz_offset_hours)
{
g_sys_time.base_unix_ts = unix_ts;
g_sys_time.sync_hal_tick = HAL_GetTick();
g_sys_time.tz_offset_hours = tz_offset_hours;
g_sys_time.is_valid = true;
}

uint32_t SysTime_GetUTCTimestamp(void)
{
if (!g_sys_time.is_valid) {
// 未校准时间时，兜底返回开机运行秒数
return HAL_GetTick() / 1000;
}

// 计算自上一次 USB 校准以来经过的增量秒数
uint32_t elapsed_sec = (HAL_GetTick() - g_sys_time.sync_hal_tick) / 1000;
return g_sys_time.base_unix_ts + elapsed_sec;


}

uint32_t SysTime_GetLocalTimestamp(void)
{
uint32_t utc = SysTime_GetUTCTimestamp();
if (!g_sys_time.is_valid) {
return utc;
}

// 当地时间戳 = UTC 时间戳 + (时区小时数 * 3600)
int32_t offset_sec = (int32_t)g_sys_time.tz_offset_hours * 3600;
return (uint32_t)((int32_t)utc + offset_sec);


}

bool SysTime_ParseUSBCommand(const char *buf, uint32_t len)
{
(void)len;
// 匹配 "TIME:<unix_timestamp>,<tz_offset>"
// 例如: "TIME:1721770000,8" 代表 2024-07-23 21:26:40 (UTC+8)
if (strncmp(buf, "TIME:", 5) == 0) {
uint32_t ts = 0;
int tz = 8;
if (sscanf(buf + 5, "%lu,%d", &ts, &tz) >= 1) {
SysTime_SetUnixTime(ts, (int8_t)tz);
return true;
}
}
return false;
}