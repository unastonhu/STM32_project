#include "sample_library.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "w25q64.h"

#define SAMPLE_JOURNAL_MAGIC       0x53414D50UL /* "SAMP" */
#define SAMPLE_JOURNAL_RECORD_SIZE 32U
#define SAMPLE_JOURNAL_CAPACITY \
    (SAMPLE_LIBRARY_FLASH_AREA_SIZE / SAMPLE_JOURNAL_RECORD_SIZE)

typedef enum {
    SAMPLE_JOURNAL_UPSERT = 1,
    SAMPLE_JOURNAL_DELETE = 2
} SampleJournalOp_t;

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t group_id;
    uint32_t start_timestamp;
    uint32_t end_timestamp;
    uint32_t model_version;
    uint8_t op;
    uint8_t label;
    uint8_t flags;
    uint8_t reserved8;
    uint16_t reserved16;
    uint16_t crc16;
} SampleJournalRecord_t;

typedef struct {
    SampleGroup_t groups[SAMPLE_LIBRARY_MAX_GROUPS];
    uint32_t group_slots;
    uint32_t active_groups;
    uint32_t append_index;
    uint32_t next_sequence;
    uint32_t next_group_id;
    uint32_t invalid_records;
    uint32_t current_model_version;
    bool initialized;
} SampleLibraryState_t;

static SampleLibraryState_t s_library;

extern osMutexId_t flash_mutex;

_Static_assert(
    sizeof(SampleJournalRecord_t) == SAMPLE_JOURNAL_RECORD_SIZE,
    "Sample journal record must remain 32 bytes"
);
_Static_assert(
    (SAMPLE_LIBRARY_FLASH_ADDR_BASE % 4096U) == 0U,
    "Sample library base must be sector aligned"
);
_Static_assert(
    (SAMPLE_LIBRARY_FLASH_AREA_SIZE % 4096U) == 0U,
    "Sample library area must contain whole sectors"
);

static bool SampleLibrary_KernelRunning(void)
{
    return osKernelGetState() == osKernelRunning;
}

static bool SampleLibrary_LockFlash(void)
{
    if (!SampleLibrary_KernelRunning()) {
        return true;
    }

    return flash_mutex != NULL &&
           osMutexAcquire(flash_mutex, osWaitForever) == osOK;
}

static void SampleLibrary_UnlockFlash(void)
{
    if (SampleLibrary_KernelRunning() && flash_mutex != NULL) {
        (void)osMutexRelease(flash_mutex);
    }
}

static void SampleLibrary_EnterCritical(void)
{
    if (SampleLibrary_KernelRunning()) {
        taskENTER_CRITICAL();
    }
}

static void SampleLibrary_ExitCritical(void)
{
    if (SampleLibrary_KernelRunning()) {
        taskEXIT_CRITICAL();
    }
}

static uint16_t SampleLibrary_Crc16(const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t crc = 0xFFFFU;

    for (uint32_t i = 0U; i < length; i++) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 0x8000U) != 0U
                ? (uint16_t)((crc << 1) ^ 0x1021U)
                : (uint16_t)(crc << 1);
        }
    }

    return crc;
}

static bool SampleLibrary_IsErased(const SampleJournalRecord_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;

    for (uint32_t i = 0U; i < sizeof(*record); i++) {
        if (bytes[i] != 0xFFU) {
            return false;
        }
    }
    return true;
}

static bool SampleLibrary_RecordValid(const SampleJournalRecord_t *record)
{
    uint16_t expected_crc;

    if (record->magic != SAMPLE_JOURNAL_MAGIC ||
        record->group_id == 0U ||
        (record->op != SAMPLE_JOURNAL_UPSERT &&
         record->op != SAMPLE_JOURNAL_DELETE)) {
        return false;
    }

    if (record->op == SAMPLE_JOURNAL_UPSERT &&
        (record->label >= SAMPLE_LABEL_COUNT ||
         record->start_timestamp > record->end_timestamp)) {
        return false;
    }

    expected_crc = SampleLibrary_Crc16(
        record,
        (uint32_t)offsetof(SampleJournalRecord_t, crc16)
    );
    return record->crc16 == expected_crc;
}

static int32_t SampleLibrary_FindSlot(uint32_t group_id)
{
    for (uint32_t i = 0U; i < s_library.group_slots; i++) {
        if (s_library.groups[i].group_id == group_id) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool SampleLibrary_Replay(const SampleJournalRecord_t *record)
{
    int32_t slot = SampleLibrary_FindSlot(record->group_id);

    if (record->op == SAMPLE_JOURNAL_DELETE) {
        if (slot >= 0 && s_library.groups[slot].active) {
            s_library.groups[slot].active = false;
            s_library.active_groups--;
        }
        return true;
    }

    if (slot < 0) {
        for (uint32_t i = 0U; i < s_library.group_slots; i++) {
            if (!s_library.groups[i].active) {
                slot = (int32_t)i;
                break;
            }
        }

        if (slot < 0) {
            if (s_library.group_slots >= SAMPLE_LIBRARY_MAX_GROUPS) {
                return false;
            }
            slot = (int32_t)s_library.group_slots++;
        }

        memset(&s_library.groups[slot], 0, sizeof(s_library.groups[slot]));
        s_library.groups[slot].group_id = record->group_id;
    }

    if (!s_library.groups[slot].active) {
        s_library.active_groups++;
    }
    s_library.groups[slot].start_timestamp = record->start_timestamp;
    s_library.groups[slot].end_timestamp = record->end_timestamp;
    s_library.groups[slot].model_version = record->model_version;
    s_library.groups[slot].label = (SampleLabel_t)record->label;
    s_library.groups[slot].active = true;
    return true;
}

static bool SampleLibrary_WriteRecord(SampleJournalRecord_t *record)
{
    SampleJournalRecord_t verify;
    uint32_t address;

    if (s_library.append_index >= SAMPLE_JOURNAL_CAPACITY) {
        return false;
    }

    record->magic = SAMPLE_JOURNAL_MAGIC;
    record->sequence = s_library.next_sequence;
    record->flags = 0U;
    record->reserved8 = 0U;
    record->reserved16 = 0U;
    record->crc16 = SampleLibrary_Crc16(
        record,
        (uint32_t)offsetof(SampleJournalRecord_t, crc16)
    );

    address = SAMPLE_LIBRARY_FLASH_ADDR_BASE +
              s_library.append_index * SAMPLE_JOURNAL_RECORD_SIZE;

    /*
     * Records are 32-byte aligned and therefore never cross a 256-byte page.
     * No sector erase occurs during normal editing: the journal only appends.
     */
    W25Q64_WritePage(
        SAMPLE_LIBRARY_FLASH_DEV_INDEX,
        (uint8_t *)record,
        address,
        sizeof(*record)
    );
    W25Q64_ReadData(
        SAMPLE_LIBRARY_FLASH_DEV_INDEX,
        (uint8_t *)&verify,
        address,
        sizeof(verify)
    );

    if (memcmp(record, &verify, sizeof(verify)) != 0) {
        /*
         * The partially programmed slot is intentionally abandoned. Recovery
         * counts it as invalid and the next edit continues in the next slot.
         */
        s_library.append_index++;
        s_library.invalid_records++;
        return false;
    }

    s_library.append_index++;
    s_library.next_sequence++;
    return true;
}

bool SampleLibrary_Init(uint32_t current_model_version)
{
    SampleJournalRecord_t record;

    memset(&s_library, 0, sizeof(s_library));
    s_library.current_model_version = current_model_version;
    s_library.next_sequence = 1U;
    s_library.next_group_id = 1U;

    for (uint32_t i = 0U; i < SAMPLE_JOURNAL_CAPACITY; i++) {
        uint32_t address = SAMPLE_LIBRARY_FLASH_ADDR_BASE +
                           i * SAMPLE_JOURNAL_RECORD_SIZE;

        W25Q64_ReadData(
            SAMPLE_LIBRARY_FLASH_DEV_INDEX,
            (uint8_t *)&record,
            address,
            sizeof(record)
        );

        if (SampleLibrary_IsErased(&record)) {
            continue;
        }

        /* Every non-erased slot is consumed, even if power loss corrupted it. */
        s_library.append_index = i + 1U;

        if (!SampleLibrary_RecordValid(&record) ||
            !SampleLibrary_Replay(&record)) {
            s_library.invalid_records++;
            continue;
        }

        if (record.sequence >= s_library.next_sequence) {
            s_library.next_sequence = record.sequence + 1U;
        }
        if (record.group_id >= s_library.next_group_id) {
            s_library.next_group_id = record.group_id + 1U;
        }
    }

    s_library.initialized = true;
    return true;
}

bool SampleLibrary_AddRange(
    uint32_t start_timestamp,
    uint32_t end_timestamp,
    SampleLabel_t label,
    uint32_t *out_group_id)
{
    SampleJournalRecord_t record = {0};
    bool written;

    if (!s_library.initialized ||
        start_timestamp > end_timestamp ||
        label >= SAMPLE_LABEL_COUNT ||
        !SampleLibrary_LockFlash()) {
        return false;
    }

    if (s_library.active_groups >= SAMPLE_LIBRARY_MAX_GROUPS) {
        SampleLibrary_UnlockFlash();
        return false;
    }

    record.group_id = s_library.next_group_id;
    record.start_timestamp = start_timestamp;
    record.end_timestamp = end_timestamp;
    record.model_version = s_library.current_model_version;
    record.op = SAMPLE_JOURNAL_UPSERT;
    record.label = (uint8_t)label;

    written = SampleLibrary_WriteRecord(&record);
    if (written) {
        SampleLibrary_EnterCritical();
        (void)SampleLibrary_Replay(&record);
        s_library.next_group_id++;
        SampleLibrary_ExitCritical();

        if (out_group_id != NULL) {
            *out_group_id = record.group_id;
        }
    }

    SampleLibrary_UnlockFlash();
    return written;
}

bool SampleLibrary_UpdateRange(
    uint32_t group_id,
    uint32_t start_timestamp,
    uint32_t end_timestamp,
    SampleLabel_t label)
{
    SampleJournalRecord_t record = {0};
    int32_t slot;
    bool written;

    if (!s_library.initialized ||
        group_id == 0U ||
        start_timestamp > end_timestamp ||
        label >= SAMPLE_LABEL_COUNT ||
        !SampleLibrary_LockFlash()) {
        return false;
    }

    slot = SampleLibrary_FindSlot(group_id);
    if (slot < 0 || !s_library.groups[slot].active) {
        SampleLibrary_UnlockFlash();
        return false;
    }

    record.group_id = group_id;
    record.start_timestamp = start_timestamp;
    record.end_timestamp = end_timestamp;
    record.model_version = s_library.current_model_version;
    record.op = SAMPLE_JOURNAL_UPSERT;
    record.label = (uint8_t)label;

    written = SampleLibrary_WriteRecord(&record);
    if (written) {
        SampleLibrary_EnterCritical();
        (void)SampleLibrary_Replay(&record);
        SampleLibrary_ExitCritical();
    }

    SampleLibrary_UnlockFlash();
    return written;
}

bool SampleLibrary_Delete(uint32_t group_id)
{
    SampleJournalRecord_t record = {0};
    int32_t slot;
    bool written;

    if (!s_library.initialized ||
        group_id == 0U ||
        !SampleLibrary_LockFlash()) {
        return false;
    }

    slot = SampleLibrary_FindSlot(group_id);
    if (slot < 0 || !s_library.groups[slot].active) {
        SampleLibrary_UnlockFlash();
        return false;
    }

    record.group_id = group_id;
    record.model_version = s_library.current_model_version;
    record.op = SAMPLE_JOURNAL_DELETE;

    written = SampleLibrary_WriteRecord(&record);
    if (written) {
        SampleLibrary_EnterCritical();
        (void)SampleLibrary_Replay(&record);
        SampleLibrary_ExitCritical();
    }

    SampleLibrary_UnlockFlash();
    return written;
}

uint32_t SampleLibrary_GetCount(void)
{
    uint32_t count;

    SampleLibrary_EnterCritical();
    count = s_library.active_groups;
    SampleLibrary_ExitCritical();
    return count;
}

bool SampleLibrary_GetByIndex(
    uint32_t active_index,
    SampleGroup_t *out_group)
{
    uint32_t found = 0U;
    bool result = false;

    if (out_group == NULL) {
        return false;
    }

    SampleLibrary_EnterCritical();
    for (uint32_t i = 0U; i < s_library.group_slots; i++) {
        if (!s_library.groups[i].active) {
            continue;
        }
        if (found == active_index) {
            *out_group = s_library.groups[i];
            result = true;
            break;
        }
        found++;
    }
    SampleLibrary_ExitCritical();
    return result;
}

bool SampleLibrary_GetById(uint32_t group_id, SampleGroup_t *out_group)
{
    bool result = false;

    if (out_group == NULL) {
        return false;
    }

    SampleLibrary_EnterCritical();
    int32_t slot = SampleLibrary_FindSlot(group_id);
    if (slot >= 0 && s_library.groups[slot].active) {
        *out_group = s_library.groups[slot];
        result = true;
    }
    SampleLibrary_ExitCritical();
    return result;
}

void SampleLibrary_GetStats(SampleLibraryStats_t *out_stats)
{
    if (out_stats == NULL) {
        return;
    }

    SampleLibrary_EnterCritical();
    out_stats->active_groups = s_library.active_groups;
    out_stats->used_journal_records = s_library.append_index;
    out_stats->invalid_journal_records = s_library.invalid_records;
    out_stats->journal_capacity = SAMPLE_JOURNAL_CAPACITY;
    out_stats->current_model_version = s_library.current_model_version;
    SampleLibrary_ExitCritical();
}
