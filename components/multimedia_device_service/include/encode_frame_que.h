#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <avdk_error.h>
#include <common/avdk_pixel_types.h>

avdk_err_t encode_frame_que_init(void);
avdk_err_t encode_frame_que_deinit(void);
avdk_err_t encode_ready_frame_que_push(frame_buffer_t *frame);
avdk_err_t encode_free_frame_que_push(frame_buffer_t *frame);
frame_buffer_t *encode_ready_frame_que_pop(uint32_t timeout);
frame_buffer_t *encode_free_frame_que_pop(void);

#ifdef __cplusplus
}
#endif