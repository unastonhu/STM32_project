#ifndef PROTOTYPE_HEAD_H
#define PROTOTYPE_HEAD_H

#include <stdbool.h>
#include <stdint.h>

#include "ai_feature_extractor.h"
#include "sample_library.h"

typedef enum {
    PROTOTYPE_RESULT_UNKNOWN = -1,
    PROTOTYPE_RESULT_FRESH = SAMPLE_LABEL_FRESH,
    PROTOTYPE_RESULT_NOT_FRESH = SAMPLE_LABEL_NOT_FRESH,
    PROTOTYPE_RESULT_SPOILED = SAMPLE_LABEL_SPOILED
} PrototypeResultLabel_t;

typedef struct {
    PrototypeResultLabel_t label;
    float confidence;
    float nearest_distance;
    uint32_t prototype_generation;
    bool valid;
} PrototypeResult_t;

typedef struct {
    uint32_t generation;
    uint32_t model_version;
    uint32_t window_count[SAMPLE_LABEL_COUNT];
    uint32_t groups_scanned;
    uint32_t groups_used;
    uint32_t groups_skipped;
    uint8_t valid_label_mask;
    bool rebuilding;
} PrototypeHeadInfo_t;

/*
 * Flash1 中的两个原型快照扇区。每次只擦写其中一个，另一个保留为掉电备份。
 * 0x4000~0xBFFF 已由动态样本日志占用，所以这里从 0xC000 开始。
 */
#define PROTOTYPE_FLASH_SLOT_A 0x00C000U
#define PROTOTYPE_FLASH_SLOT_B 0x00D000U

/* 开机读取两个副本，选择 CRC 正确且 generation 更新的一个。 */
bool PrototypeHead_Init(uint32_t model_version);

/*
 * 后台“假重训”的主入口：
 * 读取 Flash2 中各时间戳区间 -> 切成 60 秒窗口 -> 提取 16 维特征
 * -> 计算每类均值原型和各维缩放系数 -> 写入冗余 Flash 快照。
 */
bool PrototypeHead_Rebuild(void);

/* 对指定窗口分类；只有至少两个类别已经有原型时才可能返回有效类别。 */
bool PrototypeHead_ClassifyWindow(
    const ENoseFrame_t frames[AI_FEATURE_WINDOW_FRAMES],
    PrototypeResult_t *out_result
);
bool PrototypeHead_ClassifyLatest(PrototypeResult_t *out_result);

/* 提供给以后 USB/UI 查询，不直接改写旧 ENose_State_t。 */
void PrototypeHead_GetLastResult(PrototypeResult_t *out_result);
void PrototypeHead_GetInfo(PrototypeHeadInfo_t *out_info);

#endif /* PROTOTYPE_HEAD_H */
