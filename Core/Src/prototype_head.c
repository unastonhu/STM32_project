#include "prototype_head.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "flash_manager.h"
#include "w25q64.h"

#define PROTOTYPE_SNAPSHOT_MAGIC          0x50524F54UL /* "PROT" */
#define PROTOTYPE_SNAPSHOT_FORMAT_VERSION 1U
#define PROTOTYPE_HISTORY_READ_BATCH      32U
#define PROTOTYPE_TRAINING_STRIDE         15U
#define PROTOTYPE_REJECTION_DISTANCE      3.0f

/*
 * 持久化原型快照，共 296 字节：
 * - 三类各一个 16 维中心
 * - 16 个全局标准差，用于消除特征量纲差异
 * - 每类训练窗口数、模型版本、代数和 CRC32
 */
typedef struct {
    uint32_t magic;
    uint32_t format_version;
    uint32_t generation;
    uint32_t model_version;
    float prototype[SAMPLE_LABEL_COUNT][AI_FEATURE_DIMENSION];
    float scale[AI_FEATURE_DIMENSION];
    uint32_t window_count[SAMPLE_LABEL_COUNT];
    float rejection_distance;
    uint8_t valid_label_mask;
    uint8_t reserved[3];
    uint32_t crc32;
} PrototypeSnapshot_t;

/* 重建时只保存 sum/sum_sq，无需把所有窗口特征放进 RAM。 */
typedef struct {
    double sum[SAMPLE_LABEL_COUNT][AI_FEATURE_DIMENSION];
    double sum_sq[SAMPLE_LABEL_COUNT][AI_FEATURE_DIMENSION];
    uint32_t count[SAMPLE_LABEL_COUNT];
} PrototypeAccumulator_t;

typedef struct {
    PrototypeSnapshot_t snapshot;
    PrototypeResult_t last_result;
    PrototypeHeadInfo_t info;
    uint32_t model_version;
    bool initialized;
    bool rebuilding;
} PrototypeHeadState_t;

static PrototypeHeadState_t s_head;
static FlashHistoryRecord_t
    s_history_batch[PROTOTYPE_HISTORY_READ_BATCH];
static ENoseFrame_t s_training_window[AI_FEATURE_WINDOW_FRAMES];
static ENoseFrame_t s_live_window[AI_FEATURE_WINDOW_FRAMES];

extern osMutexId_t flash_mutex;

_Static_assert(
    sizeof(PrototypeSnapshot_t) == 296U,
    "Prototype snapshot layout changed"
);
_Static_assert(
    (PROTOTYPE_FLASH_SLOT_A % 4096U) == 0U &&
    (PROTOTYPE_FLASH_SLOT_B % 4096U) == 0U,
    "Prototype snapshot slots must be sector aligned"
);

static bool PrototypeHead_KernelRunning(void)
{
    return osKernelGetState() == osKernelRunning;
}

static void PrototypeHead_EnterCritical(void)
{
    if (PrototypeHead_KernelRunning()) {
        taskENTER_CRITICAL();
    }
}

static void PrototypeHead_ExitCritical(void)
{
    if (PrototypeHead_KernelRunning()) {
        taskEXIT_CRITICAL();
    }
}

static bool PrototypeHead_LockFlash(void)
{
    if (!PrototypeHead_KernelRunning()) {
        return true;
    }
    return flash_mutex != NULL &&
           osMutexAcquire(flash_mutex, osWaitForever) == osOK;
}

static void PrototypeHead_UnlockFlash(void)
{
    if (PrototypeHead_KernelRunning() && flash_mutex != NULL) {
        (void)osMutexRelease(flash_mutex);
    }
}

static uint32_t PrototypeHead_Crc32(const void *data, uint32_t length)
{
    /* 标准 CRC-32，用来拒绝掉电造成的半条快照。 */
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < length; i++) {
        crc ^= bytes[i];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static bool PrototypeHead_SnapshotValid(
    const PrototypeSnapshot_t *snapshot,
    uint32_t model_version)
{
    /* 模型版本不一致时禁止复用旧原型，避免不同特征空间互相比较。 */
    if (snapshot->magic != PROTOTYPE_SNAPSHOT_MAGIC ||
        snapshot->format_version != PROTOTYPE_SNAPSHOT_FORMAT_VERSION ||
        snapshot->model_version != model_version ||
        (snapshot->valid_label_mask &
         (uint8_t)~((1U << SAMPLE_LABEL_COUNT) - 1U)) != 0U ||
        snapshot->rejection_distance <= 0.0f ||
        !isfinite(snapshot->rejection_distance)) {
        return false;
    }

    if (snapshot->crc32 != PrototypeHead_Crc32(
            snapshot,
            (uint32_t)offsetof(PrototypeSnapshot_t, crc32))) {
        return false;
    }

    for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
        if (snapshot->scale[dim] <= 0.0f ||
            !isfinite(snapshot->scale[dim])) {
            return false;
        }
    }

    for (uint32_t label = 0U; label < SAMPLE_LABEL_COUNT; label++) {
        if ((snapshot->valid_label_mask & (1U << label)) == 0U) {
            continue;
        }
        for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
            if (!isfinite(snapshot->prototype[label][dim])) {
                return false;
            }
        }
    }
    return true;
}

static bool PrototypeHead_GenerationNewer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static void PrototypeHead_WriteBuffer(
    uint32_t address,
    const uint8_t *data,
    uint16_t length)
{
    while (length > 0U) {
        uint16_t page_remaining = (uint16_t)(256U - (address % 256U));
        uint16_t chunk = length < page_remaining ? length : page_remaining;

        W25Q64_WritePage(
            SAMPLE_LIBRARY_FLASH_DEV_INDEX,
            (uint8_t *)data,
            address,
            chunk
        );
        address += chunk;
        data += chunk;
        length -= chunk;
    }
}

static bool PrototypeHead_SaveSnapshot(PrototypeSnapshot_t *snapshot)
{
    PrototypeSnapshot_t verify;
    uint32_t target_address =
        (snapshot->generation & 1U) != 0U
            ? PROTOTYPE_FLASH_SLOT_B
            : PROTOTYPE_FLASH_SLOT_A;

    /*
     * 奇数代写 B，偶数代写 A。开始擦除目标扇区前，上一代仍完整保留
     * 在另一个扇区；写完后再读回逐字节校验。
     */
    snapshot->magic = PROTOTYPE_SNAPSHOT_MAGIC;
    snapshot->format_version = PROTOTYPE_SNAPSHOT_FORMAT_VERSION;
    snapshot->crc32 = PrototypeHead_Crc32(
        snapshot,
        (uint32_t)offsetof(PrototypeSnapshot_t, crc32)
    );

    if (!PrototypeHead_LockFlash()) {
        return false;
    }

    W25Q64_EraseSector(
        SAMPLE_LIBRARY_FLASH_DEV_INDEX,
        target_address / 4096U
    );
    PrototypeHead_WriteBuffer(
        target_address,
        (const uint8_t *)snapshot,
        sizeof(*snapshot)
    );
    W25Q64_ReadData(
        SAMPLE_LIBRARY_FLASH_DEV_INDEX,
        (uint8_t *)&verify,
        target_address,
        sizeof(verify)
    );

    PrototypeHead_UnlockFlash();
    return memcmp(snapshot, &verify, sizeof(verify)) == 0;
}

static void PrototypeHead_PublishInfo(
    const PrototypeSnapshot_t *snapshot,
    uint32_t groups_scanned,
    uint32_t groups_used,
    uint32_t groups_skipped)
{
    s_head.info.generation = snapshot->generation;
    s_head.info.model_version = snapshot->model_version;
    memcpy(
        s_head.info.window_count,
        snapshot->window_count,
        sizeof(snapshot->window_count)
    );
    s_head.info.groups_scanned = groups_scanned;
    s_head.info.groups_used = groups_used;
    s_head.info.groups_skipped = groups_skipped;
    s_head.info.valid_label_mask = snapshot->valid_label_mask;
    s_head.info.rebuilding = s_head.rebuilding;
}

bool PrototypeHead_Init(uint32_t model_version)
{
    PrototypeSnapshot_t slot_a;
    PrototypeSnapshot_t slot_b;
    bool valid_a;
    bool valid_b;

    memset(&s_head, 0, sizeof(s_head));
    s_head.model_version = model_version;
    s_head.last_result.label = PROTOTYPE_RESULT_UNKNOWN;

    W25Q64_ReadData(
        SAMPLE_LIBRARY_FLASH_DEV_INDEX,
        (uint8_t *)&slot_a,
        PROTOTYPE_FLASH_SLOT_A,
        sizeof(slot_a)
    );
    W25Q64_ReadData(
        SAMPLE_LIBRARY_FLASH_DEV_INDEX,
        (uint8_t *)&slot_b,
        PROTOTYPE_FLASH_SLOT_B,
        sizeof(slot_b)
    );

    valid_a = PrototypeHead_SnapshotValid(&slot_a, model_version);
    valid_b = PrototypeHead_SnapshotValid(&slot_b, model_version);

    /* 两个副本都有效时，使用可处理 uint32 回绕的代数比较。 */
    if (valid_a && valid_b) {
        s_head.snapshot = PrototypeHead_GenerationNewer(
            slot_b.generation,
            slot_a.generation
        ) ? slot_b : slot_a;
    } else if (valid_a) {
        s_head.snapshot = slot_a;
    } else if (valid_b) {
        s_head.snapshot = slot_b;
    } else {
        memset(&s_head.snapshot, 0, sizeof(s_head.snapshot));
        s_head.snapshot.model_version = model_version;
        s_head.snapshot.rejection_distance =
            PROTOTYPE_REJECTION_DISTANCE;
        for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
            s_head.snapshot.scale[dim] = 1.0f;
        }
    }

    PrototypeHead_PublishInfo(&s_head.snapshot, 0U, 0U, 0U);
    s_head.initialized = true;
    return true;
}

static void PrototypeHead_Accumulate(
    PrototypeAccumulator_t *accumulator,
    SampleLabel_t label,
    const float embedding[AI_FEATURE_DIMENSION])
{
    for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
        accumulator->sum[label][dim] += embedding[dim];
        accumulator->sum_sq[label][dim] +=
            (double)embedding[dim] * embedding[dim];
    }
    accumulator->count[label]++;
}

static uint32_t PrototypeHead_ProcessGroup(
    const SampleGroup_t *group,
    const FlashHistoryState_t *history,
    PrototypeAccumulator_t *accumulator)
{
    uint32_t logical_index = 0U;
    uint32_t window_count = 0U;
    uint32_t last_uptime_ms = 0U;
    uint16_t buffered = 0U;

    memset(s_training_window, 0, sizeof(s_training_window));

    /*
     * 一个样本组单独扫描，不会为半天历史建立巨大 RAM 索引。
     * 每次最多读取 32 条（1 KB）便释放 Flash 锁，让高优先级写任务先跑。
     */
    while (logical_index < history->record_count) {
        uint16_t read_count = 0U;

        if (!PrototypeHead_LockFlash()) {
            break;
        }
        bool read_ok = FlashMgr_ReadHistoryBatch(
            history,
            logical_index,
            s_history_batch,
            PROTOTYPE_HISTORY_READ_BATCH,
            &read_count
        );
        PrototypeHead_UnlockFlash();

        if (!read_ok || read_count == 0U) {
            break;
        }
        logical_index += read_count;

        for (uint16_t i = 0U; i < read_count; i++) {
            const FlashHistoryRecord_t *record = &s_history_batch[i];

            if (!FlashMgr_HistoryRecordValid(record) ||
                record->timestamp < group->start_timestamp ||
                record->timestamp > group->end_timestamp) {
                continue;
            }

            /*
             * 正常采样为 1 Hz。超过 2.5 秒的缺口或时间倒退会清空当前窗口，
             * 防止把不连续的两段数据硬拼成一个训练样本。
             */
            if (buffered > 0U &&
                (record->uptime_ms <= last_uptime_ms ||
                 (uint32_t)(record->uptime_ms - last_uptime_ms) > 2500U)) {
                buffered = 0U;
            }
            last_uptime_ms = record->uptime_ms;

            ENoseFrame_t *frame = &s_training_window[buffered++];
            frame->timestamp_ms = record->uptime_ms;
            memcpy(frame->raw, record->raw, sizeof(frame->raw));
            frame->valid_mask = record->valid_mask;
            frame->door_state = record->door_state;

            if (buffered == AI_FEATURE_WINDOW_FRAMES) {
                float embedding[AI_FEATURE_DIMENSION];

                if (AIFeatureExtractor_Extract(
                        s_training_window,
                        embedding)) {
                    PrototypeHead_Accumulate(
                        accumulator,
                        group->label,
                        embedding
                    );
                    window_count++;
                }

                /*
                 * 窗口长度 60 秒、步长 15 秒，即保留最近 45 秒形成重叠窗口。
                 * 小样本演示中这样能获得更多特征，但相邻窗口仍属于同一组。
                 */
                memmove(
                    s_training_window,
                    &s_training_window[PROTOTYPE_TRAINING_STRIDE],
                    sizeof(ENoseFrame_t) *
                        (AI_FEATURE_WINDOW_FRAMES -
                         PROTOTYPE_TRAINING_STRIDE)
                );
                buffered =
                    AI_FEATURE_WINDOW_FRAMES -
                    PROTOTYPE_TRAINING_STRIDE;
            }
        }
    }

    return window_count;
}

static void PrototypeHead_BuildSnapshot(
    const PrototypeAccumulator_t *accumulator,
    PrototypeSnapshot_t *snapshot)
{
    double global_sum[AI_FEATURE_DIMENSION] = {0};
    double global_sum_sq[AI_FEATURE_DIMENSION] = {0};
    uint32_t total_count = 0U;

    /* 每个类别的特征均值就是 Prototypical Network 中的 prototype。 */
    for (uint32_t label = 0U; label < SAMPLE_LABEL_COUNT; label++) {
        uint32_t count = accumulator->count[label];
        snapshot->window_count[label] = count;
        if (count == 0U) {
            continue;
        }

        snapshot->valid_label_mask |= (uint8_t)(1U << label);
        total_count += count;
        for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
            snapshot->prototype[label][dim] =
                (float)(accumulator->sum[label][dim] / (double)count);
            global_sum[dim] += accumulator->sum[label][dim];
            global_sum_sq[dim] += accumulator->sum_sq[label][dim];
        }
    }

    /*
     * 使用全体训练特征的逐维标准差做缩放。
     * 后续距离等价于对角协方差的 Mahalanobis 距离，比裸欧氏距离更适合
     * 当前不同尺度的统计特征；floor_scale 防止小样本方差为零。
     */
    for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
        float floor_scale =
            dim < ENOSE_NUM_CH ? 0.05f :
            dim < 3U * ENOSE_NUM_CH ? 0.01f : 0.05f;
        double mean = global_sum[dim] / (double)total_count;
        double variance =
            global_sum_sq[dim] / (double)total_count - mean * mean;

        if (variance < (double)floor_scale * floor_scale) {
            variance = (double)floor_scale * floor_scale;
        }
        snapshot->scale[dim] = sqrtf((float)variance);
    }
}

bool PrototypeHead_Rebuild(void)
{
    PrototypeAccumulator_t accumulator;
    PrototypeSnapshot_t candidate;
    FlashHistoryState_t history;
    uint32_t group_count;
    uint32_t groups_used = 0U;
    uint32_t groups_skipped = 0U;
    uint32_t total_windows = 0U;

    /* rebuilding 标志防止两个任务同时重建并抢占静态训练缓冲。 */
    if (!s_head.initialized) {
        return false;
    }

    PrototypeHead_EnterCritical();
    if (s_head.rebuilding) {
        PrototypeHead_ExitCritical();
        return false;
    }
    s_head.rebuilding = true;
    s_head.info.rebuilding = true;
    PrototypeHead_ExitCritical();

    memset(&accumulator, 0, sizeof(accumulator));
    memset(&candidate, 0, sizeof(candidate));
    candidate.generation = s_head.snapshot.generation + 1U;
    candidate.model_version = s_head.model_version;
    candidate.rejection_distance = PROTOTYPE_REJECTION_DISTANCE;

    group_count = SampleLibrary_GetCount();

    if (!PrototypeHead_LockFlash()) {
        goto rebuild_failed;
    }
    FlashMgr_GetHistoryState(&history);
    PrototypeHead_UnlockFlash();

    /*
     * 有标注但历史不足一分钟时保留旧原型并返回失败；
     * 样本组为 0 则属于用户主动清空，允许保存一个空原型快照。
     */
    if (group_count > 0U &&
        history.record_count < AI_FEATURE_WINDOW_FRAMES) {
        goto rebuild_failed;
    }

    for (uint32_t i = 0U; i < group_count; i++) {
        SampleGroup_t group;

        if (!SampleLibrary_GetByIndex(i, &group)) {
            groups_skipped++;
            continue;
        }

        uint32_t windows = PrototypeHead_ProcessGroup(
            &group,
            &history,
            &accumulator
        );
        if (windows > 0U) {
            groups_used++;
            total_windows += windows;
        } else {
            groups_skipped++;
        }
    }

    if (group_count > 0U && total_windows == 0U) {
        goto rebuild_failed;
    }

    if (total_windows > 0U) {
        PrototypeHead_BuildSnapshot(&accumulator, &candidate);
    } else {
        for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
            candidate.scale[dim] = 1.0f;
        }
    }

    /* Flash 验证成功后才一次性发布到 RAM，实时推理不会看到半成品。 */
    if (!PrototypeHead_SaveSnapshot(&candidate)) {
        goto rebuild_failed;
    }

    PrototypeHead_EnterCritical();
    s_head.snapshot = candidate;
    s_head.rebuilding = false;
    PrototypeHead_PublishInfo(
        &candidate,
        group_count,
        groups_used,
        groups_skipped
    );
    s_head.info.rebuilding = false;
    PrototypeHead_ExitCritical();
    return true;

rebuild_failed:
    PrototypeHead_EnterCritical();
    s_head.rebuilding = false;
    s_head.info.groups_scanned = group_count;
    s_head.info.groups_used = groups_used;
    s_head.info.groups_skipped = groups_skipped;
    s_head.info.rebuilding = false;
    PrototypeHead_ExitCritical();
    return false;
}

static uint8_t PrototypeHead_CountLabels(uint8_t mask)
{
    uint8_t count = 0U;
    for (uint8_t label = 0U; label < SAMPLE_LABEL_COUNT; label++) {
        if ((mask & (1U << label)) != 0U) {
            count++;
        }
    }
    return count;
}

bool PrototypeHead_ClassifyWindow(
    const ENoseFrame_t frames[AI_FEATURE_WINDOW_FRAMES],
    PrototypeResult_t *out_result)
{
    PrototypeSnapshot_t snapshot;
    PrototypeResult_t result = {
        .label = PROTOTYPE_RESULT_UNKNOWN,
        .confidence = 0.0f,
        .nearest_distance = 0.0f,
        .prototype_generation = 0U,
        .valid = false
    };
    float embedding[AI_FEATURE_DIMENSION];
    float distance_sq[SAMPLE_LABEL_COUNT] = {0};
    float score_sum = 0.0f;
    float best_distance_sq = INFINITY;
    float second_distance_sq = INFINITY;
    int32_t best_label = -1;

    if (!s_head.initialized ||
        !AIFeatureExtractor_Extract(frames, embedding)) {
        goto publish_result;
    }

    PrototypeHead_EnterCritical();
    snapshot = s_head.snapshot;
    PrototypeHead_ExitCritical();
    result.prototype_generation = snapshot.generation;

    /* 只有一个类时无法判断“像这一类”还是“所有东西都被迫归这一类”。 */
    if (PrototypeHead_CountLabels(snapshot.valid_label_mask) < 2U) {
        goto publish_result;
    }

    for (uint32_t label = 0U; label < SAMPLE_LABEL_COUNT; label++) {
        if ((snapshot.valid_label_mask & (1U << label)) == 0U) {
            distance_sq[label] = INFINITY;
            continue;
        }

        /*
         * 标准化平方欧氏距离：
         * d² = mean(((z - prototype) / scale)²)
         * 它等价于只保留协方差对角线的 Mahalanobis 距离。
         */
        float d2 = 0.0f;
        for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
            float delta =
                (embedding[dim] - snapshot.prototype[label][dim]) /
                snapshot.scale[dim];
            d2 += delta * delta;
        }
        d2 /= (float)AI_FEATURE_DIMENSION;
        distance_sq[label] = d2;

        if (d2 < best_distance_sq) {
            second_distance_sq = best_distance_sq;
            best_distance_sq = d2;
            best_label = (int32_t)label;
        } else if (d2 < second_distance_sq) {
            second_distance_sq = d2;
        }
    }

    result.nearest_distance = sqrtf(best_distance_sq);
    /*
     * 最近原型仍超过拒识半径时输出 UNKNOWN，不强迫归入三类。
     * 这对演示现场出现训练库之外的气味尤其重要。
     */
    if (best_label < 0 ||
        result.nearest_distance > snapshot.rejection_distance) {
        result.confidence = best_label < 0 ? 0.0f :
            fminf(
                1.0f,
                (result.nearest_distance - snapshot.rejection_distance) /
                snapshot.rejection_distance
            );
        goto publish_result;
    }

    for (uint32_t label = 0U; label < SAMPLE_LABEL_COUNT; label++) {
        if (isfinite(distance_sq[label])) {
            score_sum += expf(-0.5f * distance_sq[label]);
        }
    }

    float best_score = expf(-0.5f * best_distance_sq);
    float probability = score_sum > 0.0f ? best_score / score_sum : 0.0f;
    float distance_confidence =
        1.0f - result.nearest_distance / snapshot.rejection_distance;
    float margin_confidence = isfinite(second_distance_sq)
        ? fminf(
            1.0f,
            (second_distance_sq - best_distance_sq) /
                (second_distance_sq + 1.0e-6f)
        )
        : 0.0f;

    /*
     * 置信度同时考虑：
     * 1) softmax 类别概率；2) 离最近原型有多近；3) 第一、第二名间距。
     * 它是展示用可信度，不应解释成严格校准后的统计概率。
     */
    result.label = (PrototypeResultLabel_t)best_label;
    result.confidence =
        probability *
        (0.5f + 0.5f * distance_confidence) *
        (0.5f + 0.5f * margin_confidence);
    result.valid = true;

publish_result:
    PrototypeHead_EnterCritical();
    s_head.last_result = result;
    PrototypeHead_ExitCritical();
    if (out_result != NULL) {
        *out_result = result;
    }
    return result.valid;
}

bool PrototypeHead_ClassifyLatest(PrototypeResult_t *out_result)
{
    /* 使用静态窗口，避免 60 帧约 1.7 KB 数据压垮 StartEnoseTask 的栈。 */
    if (ENoseFrameBuffer_GetCount() < AI_FEATURE_WINDOW_FRAMES) {
        return false;
    }

    for (uint16_t i = 0U; i < AI_FEATURE_WINDOW_FRAMES; i++) {
        uint16_t offset =
            (uint16_t)(AI_FEATURE_WINDOW_FRAMES - 1U - i);
        if (!ENoseFrameBuffer_GetFromNewest(offset, &s_live_window[i])) {
            return false;
        }
    }

    return PrototypeHead_ClassifyWindow(s_live_window, out_result);
}

void PrototypeHead_GetLastResult(PrototypeResult_t *out_result)
{
    if (out_result == NULL) {
        return;
    }
    PrototypeHead_EnterCritical();
    *out_result = s_head.last_result;
    PrototypeHead_ExitCritical();
}

void PrototypeHead_GetInfo(PrototypeHeadInfo_t *out_info)
{
    if (out_info == NULL) {
        return;
    }
    PrototypeHead_EnterCritical();
    *out_info = s_head.info;
    PrototypeHead_ExitCritical();
}
