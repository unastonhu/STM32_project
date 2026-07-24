#include "ai_feature_extractor.h"

#include <math.h>
#include <string.h>

/* 少于 90% 有效数据的窗口不参与训练或推理，避免掉线值污染原型。 */
#define AI_FEATURE_MIN_VALID_FRAMES 54U /* 90% of a 60-second window */

static float AIFeatureExtractor_Transform(float value)
{
    /*
     * 五个传感器的量纲相差很大，例如气敏电阻可能比 TVOC 大几个数量级。
     * log1p 保留绝对高低关系，同时防止大数值通道完全支配后面的距离。
     */
    return log1pf(value > 0.0f ? value : 0.0f);
}

bool AIFeatureExtractor_Extract(
    const ENoseFrame_t frames[AI_FEATURE_WINDOW_FRAMES],
    float embedding[AI_FEATURE_DIMENSION])
{
    float sum[ENOSE_NUM_CH] = {0};
    float sum_sq[ENOSE_NUM_CH] = {0};
    float first[ENOSE_NUM_CH] = {0};
    float last[ENOSE_NUM_CH] = {0};
    uint16_t valid_count[ENOSE_NUM_CH] = {0};
    uint16_t door_closed_count = 0U;

    if (frames == NULL || embedding == NULL) {
        return false;
    }

    memset(embedding, 0, sizeof(float) * AI_FEATURE_DIMENSION);

    for (uint32_t i = 0U; i < AI_FEATURE_WINDOW_FRAMES; i++) {
        /* 当前工程中 IR status 非 0 表示门处于关闭状态。 */
        if (frames[i].door_state != 0U) {
            door_closed_count++;
        }

        for (uint32_t ch = 0U; ch < ENOSE_NUM_CH; ch++) {
            if ((frames[i].valid_mask & (1U << ch)) == 0U ||
                !isfinite(frames[i].raw[ch])) {
                continue;
            }

            float value = AIFeatureExtractor_Transform(frames[i].raw[ch]);
            if (valid_count[ch] == 0U) {
                first[ch] = value;
            }
            last[ch] = value;
            sum[ch] += value;
            sum_sq[ch] += value * value;
            valid_count[ch]++;
        }
    }

    for (uint32_t ch = 0U; ch < ENOSE_NUM_CH; ch++) {
        float mean;
        float variance;

        if (valid_count[ch] < AI_FEATURE_MIN_VALID_FRAMES) {
            return false;
        }

        mean = sum[ch] / (float)valid_count[ch];
        variance = sum_sq[ch] / (float)valid_count[ch] - mean * mean;
        if (variance < 0.0f) {
            variance = 0.0f;
        }

        embedding[ch] = mean; /* 一分钟内的气味/重量绝对水平 */
        embedding[ENOSE_NUM_CH + ch] = sqrtf(variance); /* 波动强度 */
        embedding[2U * ENOSE_NUM_CH + ch] =
            last[ch] - first[ch]; /* 一分钟内上升或下降趋势 */
    }

    embedding[15] =
        (float)door_closed_count / (float)AI_FEATURE_WINDOW_FRAMES;
    /* 开门会快速换气并产生重量扰动，因此开门超过 10% 的窗口直接丢弃。 */
    return embedding[15] >= 0.90f;
}
