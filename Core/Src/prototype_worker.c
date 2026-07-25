#include "prototype_worker.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "data_export.h"
#include "prototype_head.h"

#define PROTOTYPE_WORKER_REBUILD_FLAG  (1UL << 0)
#define PROTOTYPE_WORKER_EXPORT_FLAG   (1UL << 1)
#define PROTOTYPE_WORKER_DEBOUNCE_MS    2000U
#define PROTOTYPE_WORKER_INFERENCE_MS   3000U

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
            pdMS_TO_TICKS(PROTOTYPE_WORKER_INFERENCE_MS)
        );
        if (flags == osFlagsErrorTimeout) {
            /*
             * 没有编辑/导出请求时，每 3 秒在低优先级任务中做一次实时分类。
             * 窗口不足 60 帧、传感器掉线或原型不足两类都属于正常跳过。
             */
            if (PrototypeHead_ClassifyLatest(NULL)) {
                PrototypeWorker_Add(&s_stats.completed_inferences);
            } else {
                PrototypeWorker_Add(&s_stats.skipped_inferences);
            }
            continue;
        }
        if ((flags & osFlagsError) != 0U) {
            continue;
        }

        if ((flags & PROTOTYPE_WORKER_REBUILD_FLAG) != 0U) {
            /*
             * 等待 2 秒用于合并用户连续的增加、删除、改标签操作，避免每点一次
             * 就完整扫描一遍 Flash。等待期间到达的请求由本轮最终状态覆盖；
             * 清标志之后、实际重建期间到达的请求会保留到下一轮。
             */
            osDelay(pdMS_TO_TICKS(PROTOTYPE_WORKER_DEBOUNCE_MS));
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

        /*
         * 重建或导出可能持续超过 3 秒；完成后立即刷新一次实时结果，
         * 然后再回到带超时的等待，不需要在采样任务中补偿。
         */
        if (PrototypeHead_ClassifyLatest(NULL)) {
            PrototypeWorker_Add(&s_stats.completed_inferences);
        } else {
            PrototypeWorker_Add(&s_stats.skipped_inferences);
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
