#include "flash_worker.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "flash_manager.h"
#include "sys_time.h"

#define FLASH_WORKER_QUEUE_DEPTH   16U
#define FLASH_HISTORY_BATCH_COUNT   4U

typedef enum {
    FLASH_JOB_HISTORY_FRAME = 0,
    FLASH_JOB_ENOSE_LOG
} FlashJobType_t;

typedef struct {
    FlashJobType_t type;
    union {
        FlashHistoryRecord_t history;
        FlashLogEntry_t enose_log;
    } payload;
} FlashJob_t;

static osMessageQueueId_t s_flash_queue;
static FlashWorkerStats_t s_stats;

extern osMutexId_t flash_mutex;

static void FlashWorker_StatsAdd(uint32_t *counter, uint32_t amount)
{
    taskENTER_CRITICAL();
    *counter += amount;
    taskEXIT_CRITICAL();
}

bool FlashWorker_Init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_flash_queue = osMessageQueueNew(
        FLASH_WORKER_QUEUE_DEPTH,
        sizeof(FlashJob_t),
        NULL
    );
    return s_flash_queue != NULL;
}

bool FlashWorker_EnqueueHistoryFrame(const ENoseFrame_t *frame)
{
    FlashJob_t job = {0};

    if (frame == NULL || s_flash_queue == NULL) {
        return false;
    }

    job.type = FLASH_JOB_HISTORY_FRAME;
    job.payload.history.timestamp = SysTime_GetLocalTimestamp();
    job.payload.history.uptime_ms = frame->timestamp_ms;
    memcpy(job.payload.history.raw, frame->raw, sizeof(frame->raw));
    job.payload.history.valid_mask = frame->valid_mask;
    job.payload.history.door_state = frame->door_state;
    job.payload.history.checksum =
        FlashMgr_HistoryChecksum(&job.payload.history);

    if (osMessageQueuePut(s_flash_queue, &job, 0U, 0U) != osOK) {
        FlashWorker_StatsAdd(&s_stats.dropped_jobs, 1U);
        return false;
    }

    FlashWorker_StatsAdd(&s_stats.queued_history_frames, 1U);
    return true;
}

bool FlashWorker_EnqueueEnoseLog(
    const ENose_t *enose,
    float temp,
    float hum,
    float risk,
    float loss)
{
    FlashJob_t job = {0};

    if (enose == NULL || s_flash_queue == NULL) {
        return false;
    }

    job.type = FLASH_JOB_ENOSE_LOG;
    job.payload.enose_log.timestamp = SysTime_GetLocalTimestamp();
    memcpy(
        job.payload.enose_log.resp,
        enose->resp,
        sizeof(job.payload.enose_log.resp)
    );
    job.payload.enose_log.env_temp = temp;
    job.payload.enose_log.env_hum = hum;
    job.payload.enose_log.spoilage_risk = risk;
    job.payload.enose_log.weight_loss_g = loss;
    job.payload.enose_log.ai_state = (uint8_t)enose->state;

    if (osMessageQueuePut(s_flash_queue, &job, 0U, 0U) != osOK) {
        FlashWorker_StatsAdd(&s_stats.dropped_jobs, 1U);
        return false;
    }

    FlashWorker_StatsAdd(&s_stats.queued_enose_logs, 1U);
    return true;
}

void FlashWorker_GetStats(FlashWorkerStats_t *out_stats)
{
    if (out_stats == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    *out_stats = s_stats;
    taskEXIT_CRITICAL();
}

void FlashWorker_Task(void *argument)
{
    FlashHistoryRecord_t history_batch[FLASH_HISTORY_BATCH_COUNT];
    uint8_t history_count = 0U;
    FlashJob_t job;

    (void)argument;
    FlashMgr_HistorySessionInit();

    for (;;) {
        if (osMessageQueueGet(s_flash_queue, &job, NULL, osWaitForever) != osOK) {
            continue;
        }

        if (job.type == FLASH_JOB_HISTORY_FRAME) {
            history_batch[history_count++] = job.payload.history;

            if (history_count >= FLASH_HISTORY_BATCH_COUNT) {
                osMutexAcquire(flash_mutex, osWaitForever);
                bool written = FlashMgr_AppendHistoryBatch(
                    history_batch,
                    history_count
                );
                osMutexRelease(flash_mutex);

                if (written) {
                    FlashWorker_StatsAdd(
                        &s_stats.written_history_frames,
                        history_count
                    );
                } else {
                    FlashWorker_StatsAdd(&s_stats.dropped_jobs, history_count);
                }
                history_count = 0U;
            }
        }
        else if (job.type == FLASH_JOB_ENOSE_LOG) {
            osMutexAcquire(flash_mutex, osWaitForever);
            FlashMgr_AppendLogEntry(&job.payload.enose_log);
            osMutexRelease(flash_mutex);
            FlashWorker_StatsAdd(&s_stats.written_enose_logs, 1U);
        }
    }
}
