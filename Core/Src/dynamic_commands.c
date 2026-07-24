#include "dynamic_commands.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "data_export.h"
#include "prototype_head.h"
#include "prototype_worker.h"
#include "sample_library.h"
#include "usb_reporter.h"

static bool DynamicCommands_ParseUInt(
    const char *json,
    const char *field,
    uint32_t *out_value)
{
    char pattern[32];
    const char *position;
    char *end;
    unsigned long value;

    if (json == NULL || field == NULL || out_value == NULL) {
        return false;
    }

    (void)snprintf(pattern, sizeof(pattern), "\"%s\":", field);
    position = strstr(json, pattern);
    if (position == NULL) {
        return false;
    }

    position += strlen(pattern);
    while (*position == ' ' || *position == '\t') {
        position++;
    }
    if (*position < '0' || *position > '9') {
        return false;
    }
    value = strtoul(position, &end, 10);
    if (end == position || value > UINT32_MAX) {
        return false;
    }

    *out_value = (uint32_t)value;
    return true;
}

static bool DynamicCommands_Is(const char *json, const char *command)
{
    char pattern[48];

    (void)snprintf(
        pattern,
        sizeof(pattern),
        "\"cmd\":\"%s\"",
        command
    );
    return strstr(json, pattern) != NULL;
}

static void DynamicCommands_RequestRebuildIf(bool sample_changed)
{
    if (sample_changed) {
        /*
         * 只发送任务标志，不在 USB 任务里同步重建。
         * Worker 会等待 2 秒，把连续编辑合并为一次“假重训”。
         */
        (void)PrototypeWorker_RequestRebuild();
    }
}

bool DynamicCommands_Handle(const char *json)
{
    uint32_t id = 0U;
    uint32_t start = 0U;
    uint32_t end = 0U;
    uint32_t label = 0U;
    uint32_t index = 0U;

    if (json == NULL) {
        return false;
    }

    if (DynamicCommands_Is(json, "SAMPLE_ADD")) {
        uint32_t new_id = 0U;
        bool parsed =
            DynamicCommands_ParseUInt(json, "start", &start) &&
            DynamicCommands_ParseUInt(json, "end", &end) &&
            DynamicCommands_ParseUInt(json, "label", &label);
        bool ok = parsed && label < SAMPLE_LABEL_COUNT &&
            SampleLibrary_AddRange(
                start,
                end,
                (SampleLabel_t)label,
                &new_id
            );

        DynamicCommands_RequestRebuildIf(ok);
        (void)USB_Reporter_QueueResponse(
            "{\"cmd\":\"SAMPLE_ACK\",\"op\":\"add\","
            "\"ok\":%u,\"id\":%lu}\r\n",
            ok ? 1U : 0U,
            (unsigned long)new_id
        );
        return true;
    }

    if (DynamicCommands_Is(json, "SAMPLE_UPDATE")) {
        bool parsed =
            DynamicCommands_ParseUInt(json, "id", &id) &&
            DynamicCommands_ParseUInt(json, "start", &start) &&
            DynamicCommands_ParseUInt(json, "end", &end) &&
            DynamicCommands_ParseUInt(json, "label", &label);
        bool ok = parsed && label < SAMPLE_LABEL_COUNT &&
            SampleLibrary_UpdateRange(
                id,
                start,
                end,
                (SampleLabel_t)label
            );

        DynamicCommands_RequestRebuildIf(ok);
        (void)USB_Reporter_QueueResponse(
            "{\"cmd\":\"SAMPLE_ACK\",\"op\":\"update\","
            "\"ok\":%u,\"id\":%lu}\r\n",
            ok ? 1U : 0U,
            (unsigned long)id
        );
        return true;
    }

    if (DynamicCommands_Is(json, "SAMPLE_DELETE")) {
        bool parsed = DynamicCommands_ParseUInt(json, "id", &id);
        bool ok = parsed && SampleLibrary_Delete(id);

        DynamicCommands_RequestRebuildIf(ok);
        (void)USB_Reporter_QueueResponse(
            "{\"cmd\":\"SAMPLE_ACK\",\"op\":\"delete\","
            "\"ok\":%u,\"id\":%lu}\r\n",
            ok ? 1U : 0U,
            (unsigned long)id
        );
        return true;
    }

    if (DynamicCommands_Is(json, "SAMPLE_GET")) {
        SampleGroup_t group;
        bool parsed = DynamicCommands_ParseUInt(json, "index", &index);
        bool ok = parsed && SampleLibrary_GetByIndex(index, &group);

        if (ok) {
            (void)USB_Reporter_QueueResponse(
                "{\"cmd\":\"SAMPLE_DATA\",\"ok\":1,\"index\":%lu,"
                "\"id\":%lu,\"start\":%lu,\"end\":%lu,"
                "\"label\":%u,\"model\":%lu}\r\n",
                (unsigned long)index,
                (unsigned long)group.group_id,
                (unsigned long)group.start_timestamp,
                (unsigned long)group.end_timestamp,
                (unsigned int)group.label,
                (unsigned long)group.model_version
            );
        } else {
            (void)USB_Reporter_QueueResponse(
                "{\"cmd\":\"SAMPLE_DATA\",\"ok\":0,"
                "\"index\":%lu}\r\n",
                (unsigned long)index
            );
        }
        return true;
    }

    if (DynamicCommands_Is(json, "SAMPLE_STATS")) {
        SampleLibraryStats_t stats;
        SampleLibrary_GetStats(&stats);
        (void)USB_Reporter_QueueResponse(
            "{\"cmd\":\"SAMPLE_STATS\",\"count\":%lu,\"used\":%lu,"
            "\"capacity\":%lu,\"invalid\":%lu,\"model\":%lu}\r\n",
            (unsigned long)stats.active_groups,
            (unsigned long)stats.used_journal_records,
            (unsigned long)stats.journal_capacity,
            (unsigned long)stats.invalid_journal_records,
            (unsigned long)stats.current_model_version
        );
        return true;
    }

    if (DynamicCommands_Is(json, "AI_REBUILD")) {
        bool ok = PrototypeWorker_RequestRebuild();
        (void)USB_Reporter_QueueResponse(
            "{\"cmd\":\"AI_REBUILD_ACK\",\"ok\":%u}\r\n",
            ok ? 1U : 0U
        );
        return true;
    }

    if (DynamicCommands_Is(json, "AI_STATUS")) {
        PrototypeHeadInfo_t info;
        PrototypeResult_t result;

        PrototypeHead_GetInfo(&info);
        PrototypeHead_GetLastResult(&result);
        (void)USB_Reporter_QueueResponse(
            "{\"cmd\":\"AI_STATUS\",\"generation\":%lu,"
            "\"labels\":%u,\"windows\":[%lu,%lu,%lu],"
            "\"rebuilding\":%u,\"result\":%d,\"valid\":%u,"
            "\"confidence\":%.3f,\"distance\":%.3f,"
            "\"heap_free\":%lu,\"heap_min\":%lu}\r\n",
            (unsigned long)info.generation,
            (unsigned int)info.valid_label_mask,
            (unsigned long)info.window_count[0],
            (unsigned long)info.window_count[1],
            (unsigned long)info.window_count[2],
            info.rebuilding ? 1U : 0U,
            (int)result.label,
            result.valid ? 1U : 0U,
            result.confidence,
            result.nearest_distance,
            (unsigned long)xPortGetFreeHeapSize(),
            (unsigned long)xPortGetMinimumEverFreeHeapSize()
        );
        return true;
    }

    if (DynamicCommands_Is(json, "EXPORT_SAMPLE")) {
        bool parsed = DynamicCommands_ParseUInt(json, "id", &id);
        bool ok = parsed && DataExport_Begin(id);

        if (ok && !PrototypeWorker_RequestExport()) {
            DataExport_CancelPending();
            ok = false;
        }
        (void)USB_Reporter_QueueResponse(
            "{\"cmd\":\"EXPORT_ACK\",\"ok\":%u,\"id\":%lu}\r\n",
            ok ? 1U : 0U,
            (unsigned long)id
        );
        return true;
    }

    return false;
}
