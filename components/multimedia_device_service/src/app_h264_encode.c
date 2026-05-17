#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>

#include <avdk_error.h>
#include <components/log.h>

#include <components/bk_frame_buffer.h>
#include <components/bk_encode/bk_h264_encode_ctlr.h>
#include <components/bk_encode/bk_h264_encode_types.h>

#include "mds_img_manager.h"
#include "app_codec.h"
#include "app_jpeg_decode.h"
#if CONFIG_NTWK_H264_DROP_POLICY
#include "h264_backpressure_drop.h"
#endif

#define TAG "pipeline_test"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define PIPELINE_ENC_QUEUE_LEN 4

typedef enum {
    PIPELINE_ENC_MSG_FRAME_START = 0,
    PIPELINE_ENC_MSG_EXIT,
} pipeline_enc_msg_event_t;

typedef struct {
    pipeline_enc_msg_event_t event;
} pipeline_enc_msg_t;

typedef struct {
    uint8_t running;
    uint16_t width;
    uint16_t height;
    uint16_t aligned_height;

    uint8_t *decode_flexa_buf;
    uint8_t decode_ring_cnt;
    uint32_t blocks_per_frame;

    bk_h264_encode_ctlr_handle_t enc_ctlr_handle;
    beken_thread_t enc_thread;
    beken_queue_t enc_queue;
    beken_semaphore_t enc_sem;

    uint8_t dumped_first_iframe;
    uint8_t dumped_first_pframe;

    uint8_t dec_error;
} app_h264_encode_pipeline_ctx_t;

static app_h264_encode_pipeline_ctx_t *s_app_h264_encode_pipeline = NULL;

/* Software Flexa: buffer request callback for v2 encoder (returns frame for encoded output). */
static void *app_h264_encode_out_buffer_malloc_cb(uint32_t size, void *args)
{
    frame_buffer_t *temp_buffer = NULL;
    if (size > 0)
    {
        temp_buffer = (frame_buffer_t *)bk_encoded_data_request();
        if(temp_buffer == NULL) {
            return NULL;
        }
    }

    return temp_buffer != NULL ? temp_buffer->frame : NULL;
}

/* Software Flexa: buffer complete callback; push frame to ready queue for consumer, update context and release consumer read count. */
static uint32_t app_h264_encode_out_buffer_complete_cb(bk_h264_encode_outbuf_info_t *info)
{
    if (info == NULL) {
        return 0;
    }

    app_h264_encode_pipeline_ctx_t *ctx = (app_h264_encode_pipeline_ctx_t *)info->args;
    if (ctx == NULL || info->outbuf == NULL) {
        return 0;
    }
    uint32_t frame_size = ((sizeof(frame_buffer_t) + 63) >> 6) << 6;
    frame_buffer_t *buffer = (frame_buffer_t *)((uint8_t *)info->outbuf - frame_size);
    if (info->status != BK_OK) {
        if (buffer != NULL) {
            bk_encoded_data_free_request((uint8_t *)buffer);
        }
    }
    else
    {
        buffer->length = info->length;
        buffer->h264_type = info->type;
        buffer->fmt = PIXEL_FMT_H264;
        buffer->sequence = info->sequence;
        bk_encoded_data_complete_request((uint8_t *)buffer);
#if CONFIG_NTWK_H264_DROP_POLICY
        if (ntwk_h264_backpressure_drop_consume_force_idr()) {
            bk_h264_encode_force_idr(ctx->enc_ctlr_handle);
        }
#endif
    }
    return 0;
}

static void app_h264_encode_flexa_done_cb(uint32_t done_lines, void *args)
{
}

static void app_h264_encode_pipeline_decode_line_done_callback(uint32_t wr_cnt, void *arg)
{
}

static void app_h264_encode_pipeline_decode_notify_callback(void *arg)
{
    app_h264_encode_pipeline_ctx_t *ctx = (app_h264_encode_pipeline_ctx_t *)arg;
    if (ctx == NULL || ctx->running == 0) {
        return;
    }

    bk_h264_encode_ioctl(ctx->enc_ctlr_handle, BK_H264_ENCODE_IOCTL_SET_FLEXA_LINES_READY, (void *)ctx->blocks_per_frame);
    bk_h264_encode_force_idr(ctx->enc_ctlr_handle);

    ctx->dec_error = 1;
}

avdk_err_t app_h264_encode_open(uint16_t width, uint16_t height)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_app_h264_encode_pipeline != NULL) {
        LOGW("doorbell h264 encode already running\n");
        return AVDK_ERR_OK;
    }

    app_h264_encode_pipeline_ctx_t *ctx = (app_h264_encode_pipeline_ctx_t *)os_malloc(sizeof(app_h264_encode_pipeline_ctx_t));
    if (ctx == NULL) {
        LOGE("malloc h264 encode ctx failed\n");
        return AVDK_ERR_NOMEM;
    }
    os_memset(ctx, 0, sizeof(*ctx));

    ctx->running = 1;
    ctx->width = width;
    ctx->height = height;
    ctx->aligned_height = (uint16_t)((height + 15U) & ~15U);
    ctx->decode_flexa_buf = NULL;
    ctx->decode_ring_cnt = 0;
    ctx->blocks_per_frame = (uint32_t)ctx->aligned_height / DECODE_FLEXA_LINES;
    ctx->dumped_first_iframe = 0;
    ctx->dumped_first_pframe = 0;

    /*
     * Set global context early so encoder callbacks (header/frame output) can
     * cache header bytes even before UVC streaming is started.
     */
    s_app_h264_encode_pipeline = ctx;

    uint8_t *decode_buf = NULL;
    uint8_t ring_cnt = 0;
    ret = app_jpeg_decode_get_flexa_context(&decode_buf, &ring_cnt);
    if (ret != AVDK_ERR_OK || decode_buf == NULL) {
        LOGE("decode_test_get_decode_context failed, ret=%d\n", ret);
        ret = AVDK_ERR_GENERIC;
        goto fail;
    }
    ctx->decode_flexa_buf = decode_buf;
    ctx->decode_ring_cnt = ring_cnt;
    LOGD("decode_buf=%p, ring_cnt=%d\n", decode_buf, ring_cnt);

    ret = rtos_init_queue(&ctx->enc_queue, "h264_encode_q", sizeof(pipeline_enc_msg_t), PIPELINE_ENC_QUEUE_LEN);
    if (ret != BK_OK) {
        LOGE("init enc queue failed, ret=%d\n", ret);
        ret = AVDK_ERR_GENERIC;
        goto fail;
    }

    bk_h264_encode_ctlr_handle_t handle = NULL;

    bk_h264_encode_sw_flexa_config_t enc_config = {0};
    enc_config.width = (uint32_t)ctx->width;
    enc_config.height = (uint32_t)ctx->aligned_height;
    enc_config.input_flexa_cnt = ctx->decode_ring_cnt;
    enc_config.input_buf = (uint32_t)(uintptr_t)ctx->decode_flexa_buf;
    enc_config.input_size = (uint32_t)ctx->aligned_height;
    enc_config.input_format = BK_PIXEL_FORMAT_NV12;
    enc_config.gop_frame_count = 30;
    enc_config.outbuf_malloc = app_h264_encode_out_buffer_malloc_cb;
    enc_config.outbuf_malloc_args = ctx;
    enc_config.outbuf_complete = app_h264_encode_out_buffer_complete_cb;
    enc_config.outbuf_complete_args = ctx;
    enc_config.flexa_done = app_h264_encode_flexa_done_cb;
    enc_config.flexa_done_arg = ctx;
    ret = bk_h264_encode_sw_flexa_new(&handle, &enc_config);
    if (ret != AVDK_ERR_OK) {
        LOGE("bk_h264_encode_sw_flexa_new failed, ret=%d\n", ret);
        goto fail;
    }
    ctx->enc_ctlr_handle = handle;

    ret = bk_h264_encode_init(handle);
    if (ret != AVDK_ERR_OK) {
        LOGE("H.264 encoder init failed, ret=%d\n", ret);
        goto fail;
    }

    ret = bk_h264_encode_open(handle);
    if (ret != AVDK_ERR_OK) {
        LOGE("H.264 encoder open failed, ret=%d\n", ret);
        goto fail;
    }

    uint32_t debug_interval = 2000;
    bk_h264_encode_ioctl(handle, BK_H264_ENCODE_IOCTL_DEBUG_START, &debug_interval);

    LOGI("pipeline open ok\n");
    return AVDK_ERR_OK;

fail:
    if (ctx) {
        ctx->running = 0;
        if (s_app_h264_encode_pipeline == ctx) {
            s_app_h264_encode_pipeline = NULL;
        }

        if (ctx->enc_ctlr_handle) {
            bk_h264_encode_ioctl(ctx->enc_ctlr_handle, BK_H264_ENCODE_IOCTL_DEBUG_STOP, NULL);
            bk_h264_encode_close(ctx->enc_ctlr_handle);
            bk_h264_encode_deinit(ctx->enc_ctlr_handle);
            bk_h264_encode_delete(ctx->enc_ctlr_handle);
            ctx->enc_ctlr_handle = NULL;
        }

        if (ctx->enc_thread) {
            pipeline_enc_msg_t emsg = {.event = PIPELINE_ENC_MSG_EXIT};
            rtos_push_to_queue(&ctx->enc_queue, &emsg, BEKEN_NO_WAIT);
            rtos_delay_milliseconds(50);
        }

        if (ctx->enc_queue) {
            rtos_deinit_queue(&ctx->enc_queue);
        }

        os_free(ctx);
    }
    return ret;
}

avdk_err_t app_h264_encode_close(void)
{
    app_h264_encode_pipeline_ctx_t *ctx = s_app_h264_encode_pipeline;
    if (ctx == NULL) {
        return AVDK_ERR_OK;
    }

    ctx->running = 0;

    if (ctx->enc_thread) {
        pipeline_enc_msg_t msg = {.event = PIPELINE_ENC_MSG_EXIT};
        rtos_push_to_queue(&ctx->enc_queue, &msg, BEKEN_WAIT_FOREVER);
        rtos_delay_milliseconds(50);
    }

    if (ctx->enc_ctlr_handle) {
        bk_h264_encode_ioctl(ctx->enc_ctlr_handle, BK_H264_ENCODE_IOCTL_DEBUG_STOP, NULL);
        bk_h264_encode_close(ctx->enc_ctlr_handle);
        bk_h264_encode_deinit(ctx->enc_ctlr_handle);
        bk_h264_encode_delete(ctx->enc_ctlr_handle);
        ctx->enc_ctlr_handle = NULL;
    }

    if (ctx->enc_queue) {
        rtos_deinit_queue(&ctx->enc_queue);
    }
    os_free(ctx);
        s_app_h264_encode_pipeline = NULL;
    return AVDK_ERR_OK;
}

avdk_err_t app_h264_encode_get_handle(bk_h264_encode_ctlr_handle_t *handle)
{
    if (s_app_h264_encode_pipeline == NULL) {
        return AVDK_ERR_GENERIC;
    }
    *handle = s_app_h264_encode_pipeline->enc_ctlr_handle;
    return AVDK_ERR_OK;
}