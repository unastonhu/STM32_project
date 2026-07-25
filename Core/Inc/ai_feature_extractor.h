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

/*
 * 在创建 FreeRTOS 任务之前初始化 Cube.AI 网络和静态互斥量。
 * 返回 false 时系统仍可运行传感器与 USB，但动态原型分类会保持无效。
 */
bool AIFeatureExtractor_Init(void);
bool AIFeatureExtractor_IsReady(void);

bool AIFeatureExtractor_Extract(
    const ENoseFrame_t frames[AI_FEATURE_WINDOW_FRAMES],
    float embedding[AI_FEATURE_DIMENSION]
);

#endif /* AI_FEATURE_EXTRACTOR_H */
