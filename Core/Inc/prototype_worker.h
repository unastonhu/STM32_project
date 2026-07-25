#ifndef PROTOTYPE_WORKER_H
#define PROTOTYPE_WORKER_H

#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"

typedef struct {
    uint32_t requested_rebuilds;
    uint32_t completed_rebuilds;
    uint32_t failed_rebuilds;
    uint32_t requested_exports;
    uint32_t completed_exports;
    uint32_t failed_exports;
    uint32_t completed_inferences;
    uint32_t skipped_inferences;
} PrototypeWorkerStats_t;

/* 绑定在 freertos.c 中创建的低优先级任务。 */
void PrototypeWorker_AttachTask(osThreadId_t task_handle);
/* USB/UI 只需发请求并立即返回，不在通信任务里同步扫描 Flash。 */
bool PrototypeWorker_RequestRebuild(void);
/* 唤醒同一个低优先级任务执行已登记的数据导出，不新增 RTOS 任务。 */
bool PrototypeWorker_RequestExport(void);
void PrototypeWorker_Task(void *argument);
void PrototypeWorker_GetStats(PrototypeWorkerStats_t *out_stats);

#endif /* PROTOTYPE_WORKER_H */
