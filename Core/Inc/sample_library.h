#ifndef SAMPLE_LIBRARY_H
#define SAMPLE_LIBRARY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Dynamic sample labels are intentionally independent from the legacy
 * ENose_State_t. UNKNOWN is an inference result, not a trainable sample label.
 */
typedef enum {
    SAMPLE_LABEL_FRESH = 0,
    SAMPLE_LABEL_NOT_FRESH,
    SAMPLE_LABEL_SPOILED,
    SAMPLE_LABEL_COUNT
} SampleLabel_t;

/*
 * A group describes a user-selected interval in the current history stream.
 * Timestamps use the same local/uptime-second value stored by FlashWorker.
 * model_version identifies the fixed feature extractor that must process it.
 */
typedef struct {
    uint32_t group_id;
    uint32_t start_timestamp;
    uint32_t end_timestamp;
    uint32_t model_version;
    SampleLabel_t label;
    bool active;
} SampleGroup_t;

typedef struct {
    uint32_t active_groups;
    uint32_t used_journal_records;
    uint32_t invalid_journal_records;
    uint32_t journal_capacity;
    uint32_t current_model_version;
} SampleLibraryStats_t;

#define SAMPLE_LIBRARY_MAX_GROUPS          64U
#define SAMPLE_LIBRARY_FLASH_DEV_INDEX      0U
#define SAMPLE_LIBRARY_FLASH_ADDR_BASE 0x004000U
#define SAMPLE_LIBRARY_FLASH_AREA_SIZE 0x008000U
#define SAMPLE_LIBRARY_MODEL_VERSION        1U

/*
 * Recover all valid sample edit records from Flash.
 * Must run after W25Q64 initialization and before tasks use the library.
 */
bool SampleLibrary_Init(uint32_t current_model_version);

/* Add, edit and delete are durable before they return true. */
bool SampleLibrary_AddRange(
    uint32_t start_timestamp,
    uint32_t end_timestamp,
    SampleLabel_t label,
    uint32_t *out_group_id
);
bool SampleLibrary_UpdateRange(
    uint32_t group_id,
    uint32_t start_timestamp,
    uint32_t end_timestamp,
    SampleLabel_t label
);
bool SampleLibrary_Delete(uint32_t group_id);

/* Enumeration exposes active groups only. */
uint32_t SampleLibrary_GetCount(void);
bool SampleLibrary_GetByIndex(uint32_t active_index, SampleGroup_t *out_group);
bool SampleLibrary_GetById(uint32_t group_id, SampleGroup_t *out_group);
void SampleLibrary_GetStats(SampleLibraryStats_t *out_stats);

#endif /* SAMPLE_LIBRARY_H */
