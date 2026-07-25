#ifndef ENOSE_FRAME_BUFFER_H
#define ENOSE_FRAME_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include "enose.h"

/*
 * 120 frames at 1 Hz retain two minutes of synchronized sensor history.
 * The first Cube.AI window will use 60 frames; the extra minute allows
 * overlap, startup filtering and later window experiments.
 */
#define ENOSE_FRAME_BUFFER_CAPACITY 120U

/* valid_mask uses one bit per raw[] channel. */
#define ENOSE_FRAME_VALID_SGP40      (1U << 0)
#define ENOSE_FRAME_VALID_ENS_TVOC   (1U << 1)
#define ENOSE_FRAME_VALID_ENS_ECO2   (1U << 2)
#define ENOSE_FRAME_VALID_BME688     (1U << 3)
#define ENOSE_FRAME_VALID_HX711      (1U << 4)
#define ENOSE_FRAME_VALID_ALL        ((1U << ENOSE_NUM_CH) - 1U)

typedef struct {
    uint32_t timestamp_ms;
    float raw[ENOSE_NUM_CH];
    uint8_t valid_mask;
    uint8_t door_state;
    uint16_t reserved;
} ENoseFrame_t;

void ENoseFrameBuffer_Init(void);
void ENoseFrameBuffer_Push(const ENoseFrame_t *frame);
uint16_t ENoseFrameBuffer_GetCount(void);
bool ENoseFrameBuffer_GetLatest(ENoseFrame_t *out_frame);
bool ENoseFrameBuffer_GetFromNewest(uint16_t offset, ENoseFrame_t *out_frame);
/*
 * 一次性复制最近 count 帧，输出顺序为最旧 -> 最新。
 * 整个快照只进入一次临界区，避免分类读取中途被 1 Hz Push 改变 ring head。
 */
bool ENoseFrameBuffer_CopyLatest(
    ENoseFrame_t *out_frames,
    uint16_t count
);

#endif /* ENOSE_FRAME_BUFFER_H */
