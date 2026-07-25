#include "ai_feature_extractor.h"

#include <math.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "enose_network.h"
#include "enose_network_data.h"

/* 少于 90% 有效数据的窗口不参与训练或推理。 */
#define AI_FEATURE_MIN_VALID_FRAMES 54U /* 90% of a 60-second window */
#define AI_FEATURE_MUTEX_TIMEOUT_MS 2000U

#if AI_TRAINED_INPUT_FRAMES != AI_FEATURE_WINDOW_FRAMES
#error "Preprocess frame count does not match firmware window size"
#endif

#if AI_TRAINED_INPUT_CHANNELS != ENOSE_NUM_CH
#error "Preprocess channel count does not match ENOSE_NUM_CH"
#endif

#if AI_TRAINED_EMBEDDING_DIM != AI_FEATURE_DIMENSION
#error "Preprocess embedding size does not match firmware interface"
#endif

#if AI_ENOSE_NETWORK_IN_1_HEIGHT != AI_FEATURE_WINDOW_FRAMES
#error "Cube.AI model frame count does not match firmware window size"
#endif

#if AI_ENOSE_NETWORK_IN_1_CHANNEL != ENOSE_NUM_CH
#error "Cube.AI model channel count does not match firmware input"
#endif

#if AI_ENOSE_NETWORK_OUT_1_CHANNEL != AI_FEATURE_DIMENSION
#error "Cube.AI model output size does not match prototype head"
#endif

typedef enum {
    AI_FEATURE_STATE_NOT_INITIALIZED = 0,
    AI_FEATURE_STATE_READY,
    AI_FEATURE_STATE_FAILED
} AIFeatureState_t;

/*
 * Cube.AI 的输入、输出和中间层共用这一块 activation buffer。
 * 32 字节对齐沿用 ST 官方模板；静态分配避免挤占 FreeRTOS heap。
 */
AI_ALIGNED(32)
static uint8_t s_ai_activations[AI_ENOSE_NETWORK_DATA_ACTIVATIONS_SIZE];

static ai_handle s_ai_network = AI_HANDLE_NULL;
static ai_buffer *s_ai_inputs;
static ai_buffer *s_ai_outputs;
static AIFeatureState_t s_ai_state;

/*
 * 实时分类和样本库重建属于两个不同任务，不能同时改写 activation buffer。
 * 使用静态 mutex，不需要在 CubeMX 里另外配置 RTOS 对象。
 */
static StaticSemaphore_t s_ai_mutex_storage;
static SemaphoreHandle_t s_ai_mutex;

bool AIFeatureExtractor_Init(void)
{
    ai_handle activation_addresses[] = {
        AI_HANDLE_PTR(s_ai_activations)
    };
    ai_error error;

    if (s_ai_state == AI_FEATURE_STATE_READY) {
        return true;
    }
    if (s_ai_state == AI_FEATURE_STATE_FAILED) {
        return false;
    }

    s_ai_mutex = xSemaphoreCreateMutexStatic(&s_ai_mutex_storage);
    if (s_ai_mutex == NULL) {
        s_ai_state = AI_FEATURE_STATE_FAILED;
        return false;
    }

    /*
     * 权重地址传 NULL：生成代码会直接使用 Flash 中的 const 权重数组。
     * activation_addresses 则指向上面的静态 RAM。
     */
    error = ai_enose_network_create_and_init(
        &s_ai_network,
        activation_addresses,
        NULL
    );
    if (error.type != AI_ERROR_NONE) {
        s_ai_state = AI_FEATURE_STATE_FAILED;
        return false;
    }

    s_ai_inputs = ai_enose_network_inputs_get(s_ai_network, NULL);
    s_ai_outputs = ai_enose_network_outputs_get(s_ai_network, NULL);
    if (s_ai_inputs == NULL ||
        s_ai_outputs == NULL ||
        s_ai_inputs[0].data == AI_HANDLE_NULL ||
        s_ai_outputs[0].data == AI_HANDLE_NULL) {
        (void)ai_enose_network_destroy(s_ai_network);
        s_ai_network = AI_HANDLE_NULL;
        s_ai_state = AI_FEATURE_STATE_FAILED;
        return false;
    }

    s_ai_state = AI_FEATURE_STATE_READY;
    return true;
}

bool AIFeatureExtractor_IsReady(void)
{
    return s_ai_state == AI_FEATURE_STATE_READY;
}

static float AIFeatureExtractor_Transform(float value)
{
    /*
     * 五个传感器量纲相差很大。log1p 压缩大数值动态范围；
     * 再使用训练集 mean/std，必须与离线训练脚本完全一致。
     */
    return log1pf(value > 0.0f ? value : 0.0f);
}

bool AIFeatureExtractor_Extract(
    const ENoseFrame_t frames[AI_FEATURE_WINDOW_FRAMES],
    float embedding[AI_FEATURE_DIMENSION])
{
    uint16_t valid_count[ENOSE_NUM_CH] = {0};
    uint16_t door_closed_count = 0U;
    float *network_input;
    const float *network_output;
    bool success = false;

    if (frames == NULL ||
        embedding == NULL ||
        s_ai_state != AI_FEATURE_STATE_READY) {
        return false;
    }

    memset(embedding, 0, sizeof(float) * AI_FEATURE_DIMENSION);

    for (uint32_t i = 0U; i < AI_FEATURE_WINDOW_FRAMES; i++) {
        /* 当前工程中 IR status 非 0 表示门处于关闭状态。 */
        if (frames[i].door_state != 0U) {
            door_closed_count++;
        }

        for (uint32_t ch = 0U; ch < ENOSE_NUM_CH; ch++) {
            /*
             * valid_mask 用于统计传感器在线率；raw 仍可能是最近一次保持值。
             * 但任何 NaN/Inf 都会污染整个神经网络，必须直接拒绝该窗口。
             */
            if (!isfinite(frames[i].raw[ch])) {
                return false;
            }
            if ((frames[i].valid_mask & (1U << ch)) != 0U) {
                valid_count[ch]++;
            }
        }
    }

    for (uint32_t ch = 0U; ch < ENOSE_NUM_CH; ch++) {
        if (valid_count[ch] < AI_FEATURE_MIN_VALID_FRAMES) {
            return false;
        }
    }
    /* 开门超过 10% 会快速换气并引入称重扰动，不参与训练/分类。 */
    if (door_closed_count < AI_FEATURE_MIN_VALID_FRAMES) {
        return false;
    }

    if (xSemaphoreTake(
            s_ai_mutex,
            pdMS_TO_TICKS(AI_FEATURE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }

    network_input = (float *)s_ai_inputs[0].data;
    for (uint32_t frame = 0U;
         frame < AI_FEATURE_WINDOW_FRAMES;
         frame++) {
        for (uint32_t ch = 0U; ch < ENOSE_NUM_CH; ch++) {
            float transformed =
                AIFeatureExtractor_Transform(frames[frame].raw[ch]);
            network_input[frame * ENOSE_NUM_CH + ch] =
                (transformed - g_ai_preprocess_mean[ch]) /
                g_ai_preprocess_std[ch];
        }
    }

    if (ai_enose_network_run(
            s_ai_network,
            s_ai_inputs,
            s_ai_outputs) != 1) {
        goto release_mutex;
    }

    network_output = (const float *)s_ai_outputs[0].data;
    for (uint32_t dim = 0U; dim < AI_FEATURE_DIMENSION; dim++) {
        if (!isfinite(network_output[dim])) {
            goto release_mutex;
        }
        embedding[dim] = network_output[dim];
    }
    success = true;

release_mutex:
    (void)xSemaphoreGive(s_ai_mutex);
    if (!success) {
        memset(embedding, 0, sizeof(float) * AI_FEATURE_DIMENSION);
    }
    return success;
}
