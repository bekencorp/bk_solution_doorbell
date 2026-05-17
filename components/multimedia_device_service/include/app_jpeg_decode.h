#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <avdk_error.h>
#include <common/bk_include.h>
#include <components/bk_decode/bk_jpeg_decode_types.h>

#if CONFIG_DECODE_BUFFER_CNT
#define DECODE_BUFFER_CNT (CONFIG_DECODE_BUFFER_CNT) // Flexa ring buffer count for decode
#else
#define DECODE_BUFFER_CNT (3) // Flexa ring buffer count for decode
#endif
#define DECODE_FLEXA_LINES (16) // 16 flexa lines for decode
#define DECODE_FLEXA_ALIGN_SIZE (16) // 16 bytes for flexa align
#define DECODE_DUMP_FRAME_ENABLE (0) // 1: dump frame, 0: not dump frame

avdk_err_t app_jpeg_decode_get_flexa_context(uint8_t **decode_buffer, uint8_t *ring_buffer_cnt);

typedef enum {
    DECODE_TEST_PORT_GPU = 0,
    DECODE_TEST_PORT_H264E = 1,
    DECODE_TEST_PORT_MAX,
} decode_test_port_t;

avdk_err_t app_jpeg_decode_register_isr_callback(void (*callback)(uint32_t wr_cnt, void *arg), void *arg, decode_test_port_t port);

avdk_err_t app_jpeg_decode_register_notify_callback(void (*notify)(void *arg), void *arg, decode_test_port_t port);

avdk_err_t app_jpeg_decode_gpu_set_rd_cnt(uint32_t wr_cnt);
avdk_err_t app_jpeg_decode_h264e_set_rd_cnt(uint32_t wr_cnt);

/*
 * Signal that the given output port has finished with the current decoded frame.
 * GPU and H264E must each call this with their port id when done.
 * Decode thread waits for each active port (flexa mode).
 */
avdk_err_t app_jpeg_decode_signal_frame_done(decode_test_port_t port);

/*
 * Flexa ring-buffer read pointer coordination for multiple output ports.
 *
 * wr_cnt/rd_cnt unit: Flexa block count, where 1 block = 16 luma lines.
 * The decoder write counter (wr_cnt) is reported via decode_test_register_isr_callback().
 *
 * Each port should report how many blocks it has consumed (rd_cnt).
 * Decoder internally updates decoder read pointer using the minimum rd_cnt
 * across all enabled ports, to avoid overwriting data that is still in use.
 */
avdk_err_t app_jpeg_decode_set_port_rd_cnt(decode_test_port_t port, uint32_t rd_cnt);

/*
 * Open/close APIs for non-CLI users (doorbell decode + flexa pipeline).
 * flexa_mode: 0 = frame mode, 1 = flexa mode.
 */
avdk_err_t app_jpeg_decode_open(uint16_t width,
                                uint16_t height,
                                bk_image_format_t format,
                                uint8_t flexa_mode);

avdk_err_t app_jpeg_decode_close(void);

avdk_err_t app_jpeg_decode_get_handle(bk_jpeg_decode_ctlr_handle_t *handle);
#ifdef __cplusplus
}
#endif