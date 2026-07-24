#ifndef FLASH_WORKER_H
#define FLASH_WORKER_H

#include <stdbool.h>
#include <stdint.h>

#include "enose.h"
#include "enose_frame_buffer.h"

typedef struct {
    uint32_t queued_history_frames;
    uint32_t written_history_frames;
    uint32_t queued_enose_logs;
    uint32_t written_enose_logs;
    uint32_t dropped_jobs;
} FlashWorkerStats_t;

bool FlashWorker_Init(void);
void FlashWorker_Task(void *argument);

bool FlashWorker_EnqueueHistoryFrame(const ENoseFrame_t *frame);
bool FlashWorker_EnqueueEnoseLog(
    const ENose_t *enose,
    float temp,
    float hum,
    float risk,
    float loss
);

void FlashWorker_GetStats(FlashWorkerStats_t *out_stats);

#endif /* FLASH_WORKER_H */
