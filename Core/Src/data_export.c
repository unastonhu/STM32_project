#include "data_export.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "flash_manager.h"
#include "sample_library.h"

#define DATA_EXPORT_QUEUE_DEPTH       1U
#define DATA_EXPORT_HISTORY_BATCH    32U
#define DATA_EXPORT_LINE_BUFFER_SIZE 160U

static osMessageQueueId_t s_export_queue;
static uint32_t s_pending_group_id;
static bool s_request_pending;
static bool s_running;

/* Worker 单实例使用静态缓冲，避免约 2 KB 临时数据占满任务栈。 */
static FlashHistoryRecord_t
    s_history_batch[DATA_EXPORT_HISTORY_BATCH];
static DataExportChunk_t s_build_chunk;

extern osMutexId_t flash_mutex;

static void DataExport_SetRunning(bool running)
{
    taskENTER_CRITICAL();
    s_running = running;
    taskEXIT_CRITICAL();
}

static bool DataExport_PutChunk(const DataExportChunk_t *chunk)
{
    if (s_export_queue == NULL || chunk == NULL || chunk->length == 0U) {
        return false;
    }
    return osMessageQueuePut(
        s_export_queue,
        chunk,
        0U,
        5000U
    ) == osOK;
}

static void DataExport_StartChunk(
    const SampleGroup_t *group,
    uint32_t sequence,
    bool first)
{
    memset(&s_build_chunk, 0, sizeof(s_build_chunk));
    s_build_chunk.sequence = sequence;
    s_build_chunk.length = (uint16_t)snprintf(
        s_build_chunk.data,
        sizeof(s_build_chunk.data),
        "#EXPORT_CHUNK,id=%lu,seq=%lu,label=%u,model=%lu\r\n",
        (unsigned long)group->group_id,
        (unsigned long)sequence,
        (unsigned int)group->label,
        (unsigned long)group->model_version
    );

    if (first) {
        s_build_chunk.length += (uint16_t)snprintf(
            &s_build_chunk.data[s_build_chunk.length],
            sizeof(s_build_chunk.data) - s_build_chunk.length,
            "timestamp,uptime_ms,sgp40_raw,tvoc,eco2,bme688_gas,"
            "weight,valid_mask,door_closed\r\n"
        );
    }
}

static bool DataExport_FlushChunk(bool last)
{
    s_build_chunk.last = last ? 1U : 0U;
    return DataExport_PutChunk(&s_build_chunk);
}

static bool DataExport_AppendLine(
    const SampleGroup_t *group,
    const char *line,
    uint32_t *sequence)
{
    size_t line_length = strlen(line);

    if ((size_t)s_build_chunk.length + line_length + 96U >=
        sizeof(s_build_chunk.data)) {
        if (!DataExport_FlushChunk(false)) {
            return false;
        }
        (*sequence)++;
        DataExport_StartChunk(group, *sequence, false);
    }

    memcpy(
        &s_build_chunk.data[s_build_chunk.length],
        line,
        line_length
    );
    s_build_chunk.length += (uint16_t)line_length;
    return true;
}

static bool DataExport_AppendFooter(
    const SampleGroup_t *group,
    uint32_t *sequence,
    uint32_t exported_records,
    uint32_t invalid_records)
{
    char footer[128];

    (void)snprintf(
        footer,
        sizeof(footer),
        "#EXPORT_END,id=%lu,records=%lu,invalid=%lu\r\n",
        (unsigned long)group->group_id,
        (unsigned long)exported_records,
        (unsigned long)invalid_records
    );

    if (!DataExport_AppendLine(group, footer, sequence)) {
        return false;
    }
    return DataExport_FlushChunk(true);
}

static bool DataExport_QueueError(uint32_t group_id, const char *reason)
{
    memset(&s_build_chunk, 0, sizeof(s_build_chunk));
    s_build_chunk.last = 1U;
    s_build_chunk.length = (uint16_t)snprintf(
        s_build_chunk.data,
        sizeof(s_build_chunk.data),
        "{\"cmd\":\"EXPORT_ERROR\",\"id\":%lu,\"reason\":\"%s\"}\r\n",
        (unsigned long)group_id,
        reason
    );
    return DataExport_PutChunk(&s_build_chunk);
}

bool DataExport_Init(void)
{
    s_pending_group_id = 0U;
    s_request_pending = false;
    s_running = false;
    s_export_queue = osMessageQueueNew(
        DATA_EXPORT_QUEUE_DEPTH,
        sizeof(DataExportChunk_t),
        NULL
    );
    return s_export_queue != NULL;
}

bool DataExport_Begin(uint32_t group_id)
{
    bool accepted = false;
    uint32_t queued_chunks;

    if (group_id == 0U || s_export_queue == NULL) {
        return false;
    }

    queued_chunks = osMessageQueueGetCount(s_export_queue);
    taskENTER_CRITICAL();
    if (!s_request_pending && !s_running &&
        queued_chunks == 0U) {
        s_pending_group_id = group_id;
        s_request_pending = true;
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

void DataExport_CancelPending(void)
{
    taskENTER_CRITICAL();
    if (!s_running) {
        s_request_pending = false;
        s_pending_group_id = 0U;
    }
    taskEXIT_CRITICAL();
}

bool DataExport_RunPending(void)
{
    SampleGroup_t group;
    FlashHistoryState_t history;
    uint32_t group_id;
    uint32_t logical_index = 0U;
    uint32_t sequence = 0U;
    uint32_t exported_records = 0U;
    uint32_t invalid_records = 0U;
    char line[DATA_EXPORT_LINE_BUFFER_SIZE];
    bool success = true;

    taskENTER_CRITICAL();
    if (!s_request_pending || s_running) {
        taskEXIT_CRITICAL();
        return false;
    }
    group_id = s_pending_group_id;
    s_request_pending = false;
    s_running = true;
    taskEXIT_CRITICAL();

    if (!SampleLibrary_GetById(group_id, &group)) {
        success = DataExport_QueueError(group_id, "sample_not_found");
        DataExport_SetRunning(false);
        return success;
    }

    /*
     * 只在复制 head/count 时短暂持锁。后续每次读 1 KB 后立即释放，
     * Task_Flash 可以在批次之间继续追加新的 1 Hz 历史。
     */
    if (osMutexAcquire(flash_mutex, osWaitForever) != osOK) {
        (void)DataExport_QueueError(group_id, "flash_lock_failed");
        DataExport_SetRunning(false);
        return false;
    }
    FlashMgr_GetHistoryState(&history);
    (void)osMutexRelease(flash_mutex);

    DataExport_StartChunk(&group, sequence, true);

    while (logical_index < history.record_count) {
        uint16_t read_count = 0U;
        bool read_ok;

        if (osMutexAcquire(flash_mutex, osWaitForever) != osOK) {
            success = false;
            break;
        }
        read_ok = FlashMgr_ReadHistoryBatch(
            &history,
            logical_index,
            s_history_batch,
            DATA_EXPORT_HISTORY_BATCH,
            &read_count
        );
        (void)osMutexRelease(flash_mutex);

        if (!read_ok || read_count == 0U) {
            success = false;
            break;
        }
        logical_index += read_count;

        for (uint16_t i = 0U; i < read_count; i++) {
            const FlashHistoryRecord_t *record = &s_history_batch[i];

            if (record->timestamp < group.start_timestamp ||
                record->timestamp > group.end_timestamp) {
                continue;
            }
            if (!FlashMgr_HistoryRecordValid(record)) {
                invalid_records++;
                continue;
            }

            int length = snprintf(
                line,
                sizeof(line),
                "%lu,%lu,%.7g,%.7g,%.7g,%.7g,%.7g,%u,%u\r\n",
                (unsigned long)record->timestamp,
                (unsigned long)record->uptime_ms,
                record->raw[0],
                record->raw[1],
                record->raw[2],
                record->raw[3],
                record->raw[4],
                (unsigned int)record->valid_mask,
                (unsigned int)record->door_state
            );
            if (length <= 0 || (size_t)length >= sizeof(line) ||
                !DataExport_AppendLine(&group, line, &sequence)) {
                success = false;
                break;
            }
            exported_records++;
        }

        if (!success) {
            break;
        }
    }

    if (success) {
        success = DataExport_AppendFooter(
            &group,
            &sequence,
            exported_records,
            invalid_records
        );
    } else {
        (void)DataExport_QueueError(group_id, "history_read_failed");
    }

    DataExport_SetRunning(false);
    return success;
}

bool DataExport_TryGetChunk(DataExportChunk_t *out_chunk)
{
    if (s_export_queue == NULL || out_chunk == NULL) {
        return false;
    }
    return osMessageQueueGet(
        s_export_queue,
        out_chunk,
        NULL,
        0U
    ) == osOK;
}

bool DataExport_IsBusy(void)
{
    bool busy;

    taskENTER_CRITICAL();
    busy = s_request_pending || s_running;
    taskEXIT_CRITICAL();

    if (!busy && s_export_queue != NULL) {
        busy = osMessageQueueGetCount(s_export_queue) > 0U;
    }
    return busy;
}
