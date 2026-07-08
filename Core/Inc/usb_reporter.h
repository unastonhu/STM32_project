#ifndef __USB_REPORTER_H
#define __USB_REPORTER_H

#include "main.h"

// 🌟 初始化通讯报告器 (在 RTOS 死循环外调用一次)
void USB_Reporter_Init(void);

// 🌟 核心通讯引擎 (在 UsbTask 的死循环中高频调用)
void USB_Reporter_Routine(void);

#endif /* __USB_REPORTER_H */