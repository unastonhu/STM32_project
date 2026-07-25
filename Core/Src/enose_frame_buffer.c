#include "enose_frame_buffer.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

static ENoseFrame_t s_frames[ENOSE_FRAME_BUFFER_CAPACITY];
static uint16_t s_head;
static uint16_t s_count;

void ENoseFrameBuffer_Init(void)
{
    memset(s_frames, 0, sizeof(s_frames));
    s_head = 0U;
    s_count = 0U;
}

void ENoseFrameBuffer_Push(const ENoseFrame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    taskENTER_CRITICAL();

    s_frames[s_head] = *frame;
    s_head = (uint16_t)((s_head + 1U) % ENOSE_FRAME_BUFFER_CAPACITY);
    if (s_count < ENOSE_FRAME_BUFFER_CAPACITY) {
        s_count++;
    }

    taskEXIT_CRITICAL();
}

uint16_t ENoseFrameBuffer_GetCount(void)
{
    uint16_t count;

    taskENTER_CRITICAL();
    count = s_count;
    taskEXIT_CRITICAL();

    return count;
}

bool ENoseFrameBuffer_GetLatest(ENoseFrame_t *out_frame)
{
    return ENoseFrameBuffer_GetFromNewest(0U, out_frame);
}

bool ENoseFrameBuffer_GetFromNewest(uint16_t offset, ENoseFrame_t *out_frame)
{
    bool found = false;

    if (out_frame == NULL) {
        return false;
    }

    taskENTER_CRITICAL();

    if (offset < s_count) {
        uint16_t index = (uint16_t)(
            (s_head + ENOSE_FRAME_BUFFER_CAPACITY - 1U - offset) %
            ENOSE_FRAME_BUFFER_CAPACITY
        );
        *out_frame = s_frames[index];
        found = true;
    }

    taskEXIT_CRITICAL();
    return found;
}

bool ENoseFrameBuffer_CopyLatest(
    ENoseFrame_t *out_frames,
    uint16_t count)
{
    bool copied = false;

    if (out_frames == NULL ||
        count == 0U ||
        count > ENOSE_FRAME_BUFFER_CAPACITY) {
        return false;
    }

    taskENTER_CRITICAL();

    if (count <= s_count) {
        uint16_t start = (uint16_t)(
            (s_head + ENOSE_FRAME_BUFFER_CAPACITY - count) %
            ENOSE_FRAME_BUFFER_CAPACITY
        );
        uint16_t first_count = (uint16_t)(
            ENOSE_FRAME_BUFFER_CAPACITY - start
        );
        if (first_count > count) {
            first_count = count;
        }

        memcpy(
            out_frames,
            &s_frames[start],
            sizeof(ENoseFrame_t) * first_count
        );
        if (first_count < count) {
            memcpy(
                &out_frames[first_count],
                s_frames,
                sizeof(ENoseFrame_t) * (count - first_count)
            );
        }
        copied = true;
    }

    taskEXIT_CRITICAL();
    return copied;
}
