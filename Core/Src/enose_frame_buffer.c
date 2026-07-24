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
