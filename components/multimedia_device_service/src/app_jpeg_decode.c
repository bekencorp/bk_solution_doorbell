#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <avdk_error.h>

#include <components/bk_frame_buffer.h>
#include <components/log.h>
#include <common/avdk_pixel_types.h>
#include <lcd/lcd_mipi_hx8399c_1080x1920.h>

// #include "h264_decoder_api.h"
#include "encode_frame_que.h"
#include "app_jpeg_decode.h"
#include "components/bk_decode/bk_jpeg_decode_ctlr.h"

#define TAG "db-decode"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

typedef struct {
    uint8_t task_running;
    uint8_t flexa_mode;
    uint8_t ring_buffer_cnt;
    bk_image_format_t input_format;
    bk_pixel_format_t output_format;
    uint16_t width;
    uint16_t height;
    uint16_t aligned_height;
    bk_jpeg_decode_ctlr_handle_t decode_handle;
    beken_semaphore_t decode_sem;
    beken_thread_t decode_thread;
    uint8_t *decode_buffer;
    uint8_t *yuv_frame;

    uint8_t *table_buffer;
    uint32_t flexa_size;
    volatile uint8_t dec_done;
} app_jpeg_decode_ctx_t;

static app_jpeg_decode_ctx_t *s_app_jpeg_decode_ctx = NULL;
static void app_jpeg_decode_flexa_done_callback(uint32_t wr_cnt, void *args);

/*
 * Align allocation for decoder HW/DMA requirements.
 * rtos/os allocators do not guarantee 64-byte alignment, so we wrap hsram_malloc()
 * and store the original pointer right before the aligned address for correct free.
 */
static void *hsram_aligned_malloc(uint32_t alignment, uint32_t size)
{
    if (alignment < (uint32_t)sizeof(void *)) {
        alignment = (uint32_t)sizeof(void *);
    }

    /* alignment must be power of two */
    if ((alignment & (alignment - 1U)) != 0U) {
        return NULL;
    }

    uint32_t total = size + alignment - 1U + (uint32_t)sizeof(void *);
    void *raw = hsram_malloc(total);
    if (raw == NULL) {
        return NULL;
    }

    uintptr_t start = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (start + (alignment - 1U)) & ~((uintptr_t)alignment - 1U);
    ((void **)aligned)[-1] = raw;

    return (void *)aligned;
}

static void hsram_aligned_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    void *raw = ((void **)ptr)[-1];
    os_free(raw);
}

static void *psram_aligned_malloc(uint32_t alignment, uint32_t size)
{
    if (alignment < (uint32_t)sizeof(void *)) {
        alignment = (uint32_t)sizeof(void *);
    }

    /* alignment must be power of two */
    if ((alignment & (alignment - 1U)) != 0U) {
        return NULL;
    }

    uint32_t total = size + alignment - 1U + (uint32_t)sizeof(void *);
    void *raw = psram_malloc(total);
    if (raw == NULL) {
        return NULL;
    }

    uintptr_t start = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (start + (alignment - 1U)) & ~((uintptr_t)alignment - 1U);
    ((void **)aligned)[-1] = raw;

    return (void *)aligned;
}

static void psram_aligned_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    void *raw = ((void **)ptr)[-1];
    os_free(raw);
}

static void app_jpeg_decode_frame_done_cb(int status, void *args)
{
    (void)status;
    if (status != BK_OK) {
        LOGE("%s, %d, decode failed, status: %d\n", __func__, __LINE__, status);
        return;
    }
}

static void app_jpeg_decode_flexa_done_callback(uint32_t wr_cnt, void *args)
{
    
}

static void app_decode_thread_entry(void *arg)
{
    LOGD("%s, %d, decode thread entry\n", __func__, __LINE__);
    avdk_err_t ret = AVDK_ERR_OK;
    app_jpeg_decode_ctx_t *decoder_config = (app_jpeg_decode_ctx_t *)arg;
    frame_buffer_t *encode_buffer = NULL;
    uint8_t *decode_buffer = NULL;
    uint32_t decode_buffer_size = 0;
    uint32_t frame_size = 0;
    decoder_config->task_running = 1;
    rtos_set_semaphore(&decoder_config->decode_sem);
    while (decoder_config->task_running) {

        // GPIO_UP(2);
        if (encode_buffer == NULL) {
            encode_buffer = encode_ready_frame_que_pop(2000); // 2000ms timeout
            if (encode_buffer == NULL) {
                if (decoder_config->task_running == 0) {
                    break;
                }
                LOGE("%s, %d, get encode buffer from queue failed\n", __func__, __LINE__);
                continue;
            }
        }

        //LOGD("%s, %d, frame:%p, encode frame:%p, length:%d\n", __func__, __LINE__, encode_buffer, encode_buffer->frame, encode_buffer->length);
        // GPIO_UP(3);

        // temp code for flexa mode
        if (decoder_config->flexa_mode == false || DECODE_DUMP_FRAME_ENABLE) {
            frame_size = bk_image_size_get(decoder_config->width,
                                           decoder_config->aligned_height,
                                           decoder_config->output_format);
            if (decoder_config->yuv_frame == NULL) {
                decoder_config->yuv_frame = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, frame_size);
                if (decoder_config->yuv_frame == NULL) {
                    LOGE("%s, %d, malloc yuv frame failed\n", __func__, __LINE__);
                    continue;
                }
            }
        }

        if (decoder_config->flexa_mode == false) {
            decode_buffer_size = frame_size;
            decode_buffer = decoder_config->yuv_frame;
        }
        else {
            // flexa mode
            decode_buffer_size = bk_image_size_get(decoder_config->width,
                                                   DECODE_FLEXA_LINES * decoder_config->ring_buffer_cnt,
                                                   decoder_config->output_format);
            decode_buffer = decoder_config->decode_buffer;
        }

        // GPIO_UP(4);
        if (decoder_config->input_format == BK_IMAGE_FORMAT_MJPEG) {
            bk_jpeg_decode_input_t in = {0};
            in.stream = encode_buffer->frame;
            in.stream_len = encode_buffer->length;
            in.out_buffer = decode_buffer;
            in.out_buffer_size = decode_buffer_size;
            ret = bk_jpeg_decode_frame(decoder_config->decode_handle, &in);
        } else {
            ret = AVDK_ERR_INVAL;
        }
        // GPIO_DOWN(4);

#if DECODE_DUMP_FRAME_ENABLE
        if(ret == AVDK_ERR_OK) {
            extern void stack_mem_dump(uint32_t stack_top, uint32_t stack_bottom);
            stack_mem_dump((uint32_t)encode_buffer->frame, (uint32_t)encode_buffer->frame + encode_buffer->length);
            if (decoder_config->yuv_frame != NULL) {
                stack_mem_dump((uint32_t)decoder_config->yuv_frame, (uint32_t)decoder_config->yuv_frame + frame_size);
            }
        }
#endif

        if (decoder_config->flexa_mode == false) { // non-flexa mode
            if (ret == AVDK_ERR_OK) {
                LOGD("%s, %d, decode success, TODO FIX: send to display\n", __func__, __LINE__);
                bk_frame_buffer_free(decoder_config->yuv_frame);
            }
            else {
                LOGD("%s, %d, decode failed, ret: %d\n", __func__, __LINE__, ret);
                bk_frame_buffer_free(decoder_config->yuv_frame);
            }
        }

        // GPIO_DOWN(3);

        encode_free_frame_que_push(encode_buffer);
        decoder_config->yuv_frame = NULL;
        decode_buffer = NULL;
        encode_buffer = NULL;
        // GPIO_DOWN(2);
    }

    if (decoder_config->yuv_frame) {
        bk_frame_buffer_free(decoder_config->yuv_frame);
        decoder_config->yuv_frame = NULL;
    }

    if (encode_buffer) {
        encode_free_frame_que_push(encode_buffer);
        encode_buffer = NULL;
    }

    decoder_config->decode_thread = NULL;
    rtos_set_semaphore(&decoder_config->decode_sem);
    rtos_delete_thread(NULL);
}



avdk_err_t app_jpeg_decode_open(uint16_t width,
                                uint16_t height,
                                bk_image_format_t format,
                                uint8_t flexa_mode)
{
    avdk_err_t ret = AVDK_ERR_OK;

    app_jpeg_decode_ctx_t *decoder_config = s_app_jpeg_decode_ctx;

    if (decoder_config != NULL) {
        LOGE("%s, %d, decoder config already initialized\n", __func__, __LINE__);
        return AVDK_ERR_OK;
    }

    ret = encode_frame_que_init();
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, init encode frame queue failed\n", __func__, __LINE__);
        return ret;
    }

    decoder_config = (app_jpeg_decode_ctx_t *)os_malloc(sizeof(app_jpeg_decode_ctx_t));
    if (decoder_config == NULL) {
        LOGE("%s, %d, malloc decoder config failed\n", __func__, __LINE__);
        return AVDK_ERR_NOMEM;
    }

    os_memset(decoder_config, 0, sizeof(app_jpeg_decode_ctx_t));

    decoder_config->flexa_mode = flexa_mode;
    decoder_config->input_format = format;
    decoder_config->output_format = BK_PIXEL_FORMAT_NV12;
    decoder_config->width = width;
    decoder_config->height = height;
    decoder_config->aligned_height = (height + DECODE_FLEXA_ALIGN_SIZE - 1) & ~(DECODE_FLEXA_ALIGN_SIZE - 1);
    decoder_config->decode_handle = NULL;
    decoder_config->decode_sem = NULL;
    decoder_config->decode_thread = NULL;

    if (flexa_mode) { // flexa mode
        decoder_config->ring_buffer_cnt = DECODE_BUFFER_CNT;
        decoder_config->flexa_size = bk_image_size_get(width,
                                                       DECODE_FLEXA_LINES * DECODE_BUFFER_CNT,
                                                       decoder_config->output_format);
#if CONFIG_DECODE_BUFFER_CNT
        decoder_config->decode_buffer = (uint8_t *)psram_aligned_malloc(64, decoder_config->flexa_size);
#else
        decoder_config->decode_buffer = (uint8_t *)hsram_aligned_malloc(64, decoder_config->flexa_size);
#endif
        if (decoder_config->decode_buffer == NULL) {
            LOGE("%s, %d, malloc 64-byte aligned decode buffer:%dbytes failed\n", __func__, __LINE__, decoder_config->flexa_size);
            return AVDK_ERR_NOMEM;
        }

        LOGD("%s, %d, decode buffer:%p, size:%d\n", __func__, __LINE__, decoder_config->decode_buffer, decoder_config->flexa_size);
    }

    if (format == BK_IMAGE_FORMAT_MJPEG) {
        bk_jpeg_decode_flexa_config_t cfg = DEFAULT_JPEG_DECODE_FLEXA_CONFIG;
        cfg.frame_done_cb = app_jpeg_decode_frame_done_cb;
        cfg.frame_done_args = NULL;
        cfg.flexa_done_cb = app_jpeg_decode_flexa_done_callback;
        cfg.flexa_done_args = NULL;
        cfg.out_width = width;
        cfg.out_height = height;
        cfg.segment_height = (uint16_t)(DECODE_FLEXA_LINES / 16);
        cfg.segment_number = DECODE_BUFFER_CNT;

        ret = bk_jpeg_decode_flexa_ctlr_new(&decoder_config->decode_handle, &cfg);
        if (ret != AVDK_ERR_OK) {
            LOGE("%s, %d, bk_jpeg_decode_flexa_ctlr_new failed, ret=%d\n", __func__, __LINE__, ret);
            goto out;
        }
        ret = bk_jpeg_decode_init(decoder_config->decode_handle);
        if (ret != AVDK_ERR_OK) {
            LOGE("%s, %d, bk_jpeg_decode_init failed, ret=%d\n", __func__, __LINE__, ret);
            goto out;
        }

        ret = bk_jpeg_decode_open(decoder_config->decode_handle);
        if (ret != AVDK_ERR_OK) {
            LOGE("%s, %d, bk_jpeg_decode_open failed, ret=%d\n", __func__, __LINE__, ret);
        }
    } else {
        ret = AVDK_ERR_INVAL;
    }

    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, init decoder context failed, ret=%d\n", __func__, __LINE__, ret);
        goto out;
    }

    ret = rtos_init_semaphore(&decoder_config->decode_sem, 1);
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, init decode sem failed\n", __func__, __LINE__);
        goto out;
    }

    ret = rtos_create_thread(&decoder_config->decode_thread,
                            BEKEN_DEFAULT_WORKER_PRIORITY,
                            "decode_thread",
                            (beken_thread_function_t)app_decode_thread_entry,
                            1024 * 4, // 4KB
                            decoder_config);
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, create decode thread failed\n", __func__, __LINE__);
        goto out;
    }

    rtos_get_semaphore(&decoder_config->decode_sem, BEKEN_WAIT_FOREVER);

    s_app_jpeg_decode_ctx = decoder_config;

    LOGD("%s, %d, decode turn on complete\n", __func__, __LINE__);

    return ret;

out:
    if (decoder_config) {
        if (decoder_config->decode_handle) {
            (void)bk_jpeg_decode_close(decoder_config->decode_handle);
            (void)bk_jpeg_decode_deinit(decoder_config->decode_handle);
            (void)bk_jpeg_decode_delete(decoder_config->decode_handle);
            decoder_config->decode_handle = NULL;
        }

        if (decoder_config->decode_sem) {
            rtos_deinit_semaphore(&decoder_config->decode_sem);
        }

        if (decoder_config->flexa_mode && decoder_config->decode_buffer) {
            hsram_aligned_free(decoder_config->decode_buffer);
            decoder_config->decode_buffer = NULL;
        }

        os_free(decoder_config);
        s_app_jpeg_decode_ctx = NULL;
    }

    return ret;
}

avdk_err_t app_jpeg_decode_close(void)
{
    app_jpeg_decode_ctx_t *decoder_config = s_app_jpeg_decode_ctx;

    if (decoder_config == NULL) {
        LOGE("%s, %d, decode context not initialized\n", __func__, __LINE__);
        return AVDK_ERR_OK;
    }

    LOGD("%s, %d, decode turn off start\n", __func__, decoder_config->task_running);

    decoder_config->task_running = 0;
    encode_ready_frame_que_wakeup();
    rtos_get_semaphore(&decoder_config->decode_sem, BEKEN_WAIT_FOREVER);

    if (decoder_config->input_format == BK_IMAGE_FORMAT_MJPEG) {
        (void)bk_jpeg_decode_close(decoder_config->decode_handle);
        (void)bk_jpeg_decode_deinit(decoder_config->decode_handle);
        (void)bk_jpeg_decode_delete(decoder_config->decode_handle);
    }
    decoder_config->decode_handle = NULL;

    if (decoder_config->decode_sem) {
        rtos_deinit_semaphore(&decoder_config->decode_sem);
    }

    if (decoder_config->flexa_mode && decoder_config->decode_buffer) {
        hsram_aligned_free(decoder_config->decode_buffer);
        decoder_config->decode_buffer = NULL;
    }

    os_free(decoder_config);
    s_app_jpeg_decode_ctx = NULL;

    LOGD("%s, %d, decode turn off complete\n", __func__, __LINE__);
    return AVDK_ERR_OK;
}

avdk_err_t app_jpeg_decode_get_flexa_context(uint8_t **decode_buffer, uint8_t *ring_buffer_cnt)
{
    app_jpeg_decode_ctx_t *decoder_config = s_app_jpeg_decode_ctx;
    if (decoder_config == NULL) {
        LOGE("%s, %d, decode config not initialized\n", __func__, __LINE__);
        return AVDK_ERR_UNKNOWN;
    }
    *decode_buffer = decoder_config->decode_buffer;
    *ring_buffer_cnt = decoder_config->ring_buffer_cnt;
    return AVDK_ERR_OK;
}

avdk_err_t app_jpeg_decode_get_handle(bk_jpeg_decode_ctlr_handle_t *handle)
{
    if (s_app_jpeg_decode_ctx == NULL) {
        return AVDK_ERR_GENERIC;
    }
    *handle = s_app_jpeg_decode_ctx->decode_handle;
    return AVDK_ERR_OK;
}