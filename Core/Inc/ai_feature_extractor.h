#ifndef AI_FEATURE_EXTRACTOR_H
#define AI_FEATURE_EXTRACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ai_preprocess_config.h"
#include "enose_frame_buffer.h"

/* Cube.AI 使用最近 60 秒的 5 通道同步数据，输出固定 16 维特征。 */
#define AI_FEATURE_WINDOW_FRAMES 60U
#define AI_FEATURE_DIMENSION     16U

/*
 * 版本号同时写入样本库和原型快照。更换模型或归一化参数时必须递增，
 * 这样旧模型生成的特征不会和新模型特征混用。
 */
#define AI_FEATURE_EXTRACTOR_VERSION AI_TRAINED_MODEL_VERSION

typedef struct {
    uint32_t successful_inferences;
    uint32_t failed_inferences;
    uint32_t rejected_windows;
    uint32_t mutex_timeouts;
    uint32_t model_version;
    uint16_t input_elements;
    uint16_t output_elements;
    uint8_t ready;
    uint8_t self_test_passed;
    uint8_t smoke_test_model;
    uint8_t last_error_type;
    uint8_t last_error_code;
} AIFeatureExtractorStatus_t;

/*
 * 在创建 FreeRTOS 任务之前初始化 Cube.AI 网络和静态互斥量。
 * 返回 false 时系统仍可运行传感器与 USB，但动态原型分类会保持无效。
 */
bool AIFeatureExtractor_Init(void);
bool AIFeatureExtractor_IsReady(void);
void AIFeatureExtractor_GetStatus(
    AIFeatureExtractorStatus_t *out_status
);

bool AIFeatureExtractor_Extract(
    const ENoseFrame_t frames[AI_FEATURE_WINDOW_FRAMES],
    float embedding[AI_FEATURE_DIMENSION]
);

#endif /* AI_FEATURE_EXTRACTOR_H */
