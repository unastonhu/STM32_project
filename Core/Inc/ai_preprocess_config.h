#ifndef AI_PREPROCESS_CONFIG_H
#define AI_PREPROCESS_CONFIG_H

/*
 * 当前数值只服务于 Cube.AI 集成冒烟测试模型：
 *   y = (log1p(max(raw, 0)) - mean) / std
 *
 * 现在使用 mean=0、std=1，仅验证 MCU 上的 60x5 -> 16 数据流能否跑通，
 * 不能据此宣称新鲜度识别准确。正式采集并训练后，训练脚本会生成真实的
 * mean/std，并用该文件替换这里的占位参数。
 */
#define AI_TRAINED_MODEL_VERSION   2U
#define AI_TRAINED_INPUT_FRAMES    60U
#define AI_TRAINED_INPUT_CHANNELS  5U
#define AI_TRAINED_EMBEDDING_DIM   16U
#define AI_TRAINED_ARTIFACT_IS_SMOKE_TEST 1U

static const float g_ai_preprocess_mean[AI_TRAINED_INPUT_CHANNELS] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f
};

static const float g_ai_preprocess_std[AI_TRAINED_INPUT_CHANNELS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};

#endif /* AI_PREPROCESS_CONFIG_H */
