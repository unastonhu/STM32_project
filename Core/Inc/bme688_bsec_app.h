#ifndef __BME688_BSEC_APP_H
#define __BME688_BSEC_APP_H

#ifdef __cplusplus
extern "C" {
#endif

// 声明 BSEC AI 算法主任务线程，供给 freertos.c 或 main.c 调用
void BME688_BSEC_Task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __BME688_BSEC_APP_H */
