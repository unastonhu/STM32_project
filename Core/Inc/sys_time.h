/*
*
*@file    sys_time.h
*
*@brief   基于 USB 外接同步的轻量级 UNIX 时间戳与时区管理模块
*/

#ifndef __SYS_TIME_H
#define __SYS_TIME_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
uint32_t base_unix_ts;    // USB 传入的基准 UNIX 时间戳 (UTC 秒)
uint32_t sync_hal_tick;   // 同步时刻的 HAL_GetTick() (毫秒)
int8_t   tz_offset_hours; // 时区偏移量 (如 +8 代表 北京时间 UTC+8)
bool     is_valid;        // 时间有效性标志 (true: 已同步, false: 未同步)
} SysTime_t;

extern SysTime_t g_sys_time;

/*
*
*@brief 初始化系统时间模块
*/
void SysTime_Init(void);

/*
*
*@brief 通过 USB 指令设置基准 UNIX 时间戳和时区
*
*@param unix_ts UNIX 时间戳 (UTC 秒)
*
*@param tz_offset_hours 时区 (例如 8 代表 UTC+8)
*/
void SysTime_SetUnixTime(uint32_t unix_ts, int8_t tz_offset_hours);

/*
*
*@brief 获取当前 UTC UNIX 时间戳 (秒)
*/
uint32_t SysTime_GetUTCTimestamp(void);

/*
*
*@brief 获取当前当地时间戳 (考虑时区偏移后的秒数)
*/
uint32_t SysTime_GetLocalTimestamp(void);

/*
*
*@brief 解析 USB 串口接收到的时间指令 (例如: "TIME:1721770000,8")
*
*@param buf 串口接收数据缓冲区
*
*@param len 缓冲区数据长度
*
*@return true 解析成功并已更新时间, false 非时间指令
*/
bool SysTime_ParseUSBCommand(const char *buf, uint32_t len);

#endif /* __SYS_TIME_H */
