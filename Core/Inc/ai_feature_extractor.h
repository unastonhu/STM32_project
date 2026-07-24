#ifndef AI_FEATURE_EXTRACTOR_H
#define AI_FEATURE_EXTRACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "enose_frame_buffer.h"

/* Cube.AI 计划使用最近 60 秒的 5 通道同步数据，输出固定 16 维特征。 */
#define AI_FEATURE_WINDOW_FRAMES 60U
#define AI_FEATURE_DIMENSION     16U

/*
 * 第 1 版暂时使用轻量统计特征：
 * [0..4]   5 通道对数均值
 * [5..9]   5 通道标准差
 * [10..14] 5 通道首尾趋势
 * [15]     关门数据占比
 *
 * 接口和维度从现在开始固定。以后换成 Cube.AI 1D-CNN 时只替换 .c 内部，
 * 动态原型头、样本库和上位机协议都不需要跟着改。
 */
#define AI_FEATURE_EXTRACTOR_VERSION 1U

bool AIFeatureExtractor_Extract(
    const ENoseFrame_t frames[AI_FEATURE_WINDOW_FRAMES],
    float embedding[AI_FEATURE_DIMENSION]
);

#endif /* AI_FEATURE_EXTRACTOR_H */
