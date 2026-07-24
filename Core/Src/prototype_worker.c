#include "prototype_worker.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "data_export.h"
#include "prototype_head.h"

#define PROTOTYPE_WORKER_REBUILD_FLAG  (1UL << 0)
#define PROTOTYPE_WORKER_EXPORT_FLAG   (1UL << 1)
#define PROTOTYPE_WORKER_DEBOUNCE_MS    2000U

static osThreadId_t s_task_handle;
static PrototypeWorkerStats_t s_stats;

static void PrototypeWorker_Add(uint32_t *counter)
{
    taskENTER_CRITICAL();
    (*counter)++;
    taskEXIT_CRITICAL();
}

void PrototypeWorker_AttachTask(osThreadId_t task_handle)
{
    s_task_handle = task_handle;
    memset(&s_stats, 0, sizeof(s_stats));
}

bool PrototypeWorker_RequestRebuild(void)
{
    if (s_task_handle == NULL) {
        return false;
    }

    uint32_t result = osThreadFlagsSet(
        s_task_handle,
        PROTOTYPE_WORKER_REBUILD_FLAG
    );
    if ((result & osFlagsError) != 0U) {
        return false;
    }

    PrototypeWorker_Add(&s_stats.requested_rebuilds);
    return true;
}

bool PrototypeWorker_RequestExport(void)
{
    if (s_task_handle == NULL) {
        return false;
    }

    uint32_t result = osThreadFlagsSet(
        s_task_handle,
        PROTOTYPE_WORKER_EXPORT_FLAG
    );
    if ((result & osFlagsError) != 0U) {
        return false;
    }

    PrototypeWorker_Add(&s_stats.requested_exports);
    return true;
}

void PrototypeWorker_Task(void *argument)
{
    (void)argument;

    for (;;) {
        uint32_t flags = osThreadFlagsWait(
            PROTOTYPE_WORKER_REBUILD_FLAG |
                PROTOTYPE_WORKER_EXPORT_FLAG,
            osFlagsWaitAny,
            osWaitForever
        );
        if ((flags & osFlagsError) != 0U) {
            continue;
        }

        if ((flags & PROTOTYPE_WORKER_REBUILD_FLAG) != 0U) {
            /*
             * 等待 2 秒用于合并用户连续的增加、删除、改标签操作，避免每点一次
             * 就完整扫描一遍 Flash。重建过程中到达的新请求会保留到下一轮。
             */
            osDelay(PROTOTYPE_WORKER_DEBOUNCE_MS);
            (void)osThreadFlagsClear(PROTOTYPE_WORKER_REBUILD_FLAG);

            if (PrototypeHead_Rebuild()) {
                PrototypeWorker_Add(&s_stats.completed_rebuilds);
            } else {
                PrototypeWorker_Add(&s_stats.failed_rebuilds);
            }
        }

        if ((flags & PROTOTYPE_WORKER_EXPORT_FLAG) != 0U) {
            if (DataExport_RunPending()) {
                PrototypeWorker_Add(&s_stats.completed_exports);
            } else {
                PrototypeWorker_Add(&s_stats.failed_exports);
            }
        }
    }
}

void PrototypeWorker_GetStats(PrototypeWorkerStats_t *out_stats)
{
    if (out_stats == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    *out_stats = s_stats;
    taskEXIT_CRITICAL();
}
