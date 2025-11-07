/**
 * @file yuv_display.c
 * @brief YUV Direct Display Module
 *
 * This module handles YUV data directly output from camera, supports hardware
 * rotation and direct display to LCD. Avoids encoding/decoding overhead and
 * improves local display performance.
 *
 * @author Beken
 * @date 2025
 */

#include "yuv_display/yuv_display.h"
#include "frame/frame_que_v2.h"
#include "frame_buffer.h"
#include <driver/rott_driver.h>
#include <os/os.h>
#include <os/mem.h>
#include <components/media_types.h>

#define TAG "yuv_disp"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

// Debug log switch
#define YUV_DISPLAY_DEBUG_LOG 0

#if YUV_DISPLAY_DEBUG_LOG
#define LOG_DEBUG(...) LOGI(__VA_ARGS__)
#else
#define LOG_DEBUG(...) do {} while(0)
#endif

// YUV display task structure
typedef struct
{
    beken_thread_t task_handle;
    beken_queue_t queue;
    beken_semaphore_t sem;
    bool task_running;
    bool rotate_enabled;
    media_rotate_t rotate_angle;
    uint16_t src_width;
    uint16_t src_height;
    frame_buffer_t *rotate_output_frame;
    bk_display_ctlr_handle_t display_handle;
} yuv_display_task_t;

// Message event types
typedef enum
{
    YUV_DISPLAY_EVENT_START = 0,
    YUV_DISPLAY_EVENT_STOP,
    YUV_DISPLAY_EVENT_FRAME_READY,
} yuv_display_event_t;

// Message structure
typedef struct
{
    yuv_display_event_t event;
    void *param;
} yuv_display_msg_t;

// Global task handle
static yuv_display_task_t *s_yuv_display = NULL;

// Convert rotation angle
static media_rotate_t get_rotate_angle(uint32_t rotate)
{
    switch (rotate)
    {
        case 0:
            return ROTATE_NONE;
        case 90:
            return ROTATE_90;
        case 180:
            return ROTATE_180;
        case 270:
            return ROTATE_270;
        default:
            return ROTATE_NONE;
    }
}

// YUV rotation complete callback (hardware interrupt)
static void yuv_display_rotate_complete_cb(void)
{
    if (s_yuv_display && s_yuv_display->task_running)
    {
        yuv_display_msg_t msg;
        msg.event = YUV_DISPLAY_EVENT_FRAME_READY;
        msg.param = NULL;
        rtos_push_to_queue(&s_yuv_display->queue, &msg, BEKEN_NO_WAIT);
    }
}

// YUV frame display free callback
static bk_err_t yuv_display_frame_free_cb(void *frame)
{
    LOG_DEBUG("%s: frame:%p\n", __func__, frame);
    if (frame)
    {
        frame_queue_v2_release_frame(IMAGE_YUV, CONSUMER_DECODER, (frame_buffer_t *)frame);
    }
    return BK_OK;
}

// Rotate output frame display free callback
static bk_err_t yuv_display_rotate_frame_free_cb(void *frame)
{
    LOG_DEBUG("%s: frame:%p\n", __func__, frame);
    if (frame)
    {
        frame_buffer_display_free((frame_buffer_t *)frame);
    }
    return BK_OK;
}

/**
 * @brief Process single YUV frame
 *
 * Based on rotation config, decide to start hardware rotation or direct display
 */
static bk_err_t yuv_display_process_frame(frame_buffer_t *yuv_frame)
{
    bk_err_t ret = BK_OK;

    if (!s_yuv_display || !yuv_frame)
    {
        LOGE("%s: invalid parameters\n", __func__);
        return BK_FAIL;
    }

    LOG_DEBUG("%s: yuv_frame:%p, size:%dx%d, fmt:%d, rotate:%d\n",
              __func__, yuv_frame, yuv_frame->width, yuv_frame->height,
              yuv_frame->fmt, s_yuv_display->rotate_angle);

    // If hardware rotation needed (90 or 270 degrees)
    if (s_yuv_display->rotate_enabled &&
        (s_yuv_display->rotate_angle == ROTATE_90 || s_yuv_display->rotate_angle == ROTATE_270))
    {
        // Allocate rotation output frame buffer
        if (s_yuv_display->rotate_output_frame == NULL)
        {
            uint32_t out_size = yuv_frame->width * yuv_frame->height * 2; // YUV422 format
            s_yuv_display->rotate_output_frame = frame_buffer_display_malloc(out_size);

            if (s_yuv_display->rotate_output_frame == NULL)
            {
                LOGE("%s: malloc rotate output frame failed\n", __func__);
                return BK_FAIL;
            }

            // Set output frame properties (90/270 degree rotation swaps width and height)
            s_yuv_display->rotate_output_frame->width = yuv_frame->height;
            s_yuv_display->rotate_output_frame->height = yuv_frame->width;
            s_yuv_display->rotate_output_frame->fmt = PIXEL_FMT_RGB565_LE;
        }

        // Configure hardware rotation parameters
        rott_config_t rott_cfg = {0};
        rott_cfg.input_addr = yuv_frame->frame;
        rott_cfg.output_addr = s_yuv_display->rotate_output_frame->frame;
        rott_cfg.rot_mode = s_yuv_display->rotate_angle;
        rott_cfg.input_fmt = yuv_frame->fmt;
        rott_cfg.picture_xpixel = yuv_frame->width;
        rott_cfg.picture_ypixel = yuv_frame->height;
        rott_cfg.input_flow = ROTT_INPUT_NORMAL;
        rott_cfg.output_flow = ROTT_OUTPUT_NORMAL;

        ret = rott_config(&rott_cfg);
        if (ret != BK_OK)
        {
            LOGE("%s: rott_config failed, ret:%d\n", __func__, ret);
            // Config failed, need to release input frame
            return BK_FAIL;
        }

        // Start hardware rotation
        ret = bk_rott_enable();
        if (ret != BK_OK)
        {
            LOGE("%s: bk_rott_enable failed, ret:%d\n", __func__, ret);
            // Start failed, need to release input frame
            return BK_FAIL;
        }

        LOG_DEBUG("%s: hardware rotation started\n", __func__);
    }
    else
    {
        // No rotation or software rotation (180 degrees), direct display
        if (s_yuv_display->display_handle)
        {
            ret = bk_display_flush(s_yuv_display->display_handle, (void *)yuv_frame, yuv_display_frame_free_cb);
            if (ret != BK_OK)
            {
                LOGE("%s: bk_display_flush failed, ret:%d\n", __func__, ret);
                yuv_display_frame_free_cb(yuv_frame);
            }
            else
            {
                LOG_DEBUG("%s: display flush OK (no rotation)\n", __func__);
            }
        }
        else
        {
            LOG_DEBUG("%s: no display handle, free frame\n", __func__);
            yuv_display_frame_free_cb(yuv_frame);
        }
    }

    return ret;
}

/**
 * @brief Handle rotation complete event
 *
 * After hardware rotation complete, release input frame and display rotated frame
 */
static bk_err_t yuv_display_rotation_complete(frame_buffer_t *src_frame)
{
    bk_err_t ret = BK_OK;

    LOG_DEBUG("%s: rotation complete\n", __func__);

    if (!s_yuv_display || !s_yuv_display->rotate_output_frame)
    {
        LOGE("%s: invalid state\n", __func__);
        return BK_FAIL;
    }

    // Release input frame
    if (src_frame)
    {
        frame_queue_v2_release_frame(IMAGE_YUV, CONSUMER_DECODER, src_frame);
    }

    // Display rotated frame
    if (s_yuv_display->display_handle)
    {
        ret = bk_display_flush(s_yuv_display->display_handle,
                               (void *)s_yuv_display->rotate_output_frame,
                               yuv_display_rotate_frame_free_cb);
        if (ret != BK_OK)
        {
            LOGE("%s: bk_display_flush failed, ret:%d\n", __func__, ret);
            yuv_display_rotate_frame_free_cb(s_yuv_display->rotate_output_frame);
        }
        else
        {
            LOG_DEBUG("%s: display flush OK (rotated frame)\n", __func__);
        }

        s_yuv_display->rotate_output_frame = NULL;
    }
    else
    {
        LOG_DEBUG("%s: no display handle, free frame\n", __func__);
        yuv_display_rotate_frame_free_cb(s_yuv_display->rotate_output_frame);
        s_yuv_display->rotate_output_frame = NULL;
    }

    return ret;
}

/**
 * @brief YUV display task main loop
 *
 * Get frames from YUV queue, process rotation and display
 */
static void yuv_display_task_main(void *param)
{
    bk_err_t ret = BK_OK;
    yuv_display_msg_t msg;
    frame_buffer_t *yuv_frame = NULL;
    frame_buffer_t *processing_frame = NULL;

    LOGI("%s: task started\n", __func__);

    s_yuv_display->task_running = true;
    rtos_set_semaphore(&s_yuv_display->sem);

    while (s_yuv_display->task_running)
    {
        // First check message queue (rotation complete or stop event)
        ret = rtos_pop_from_queue(&s_yuv_display->queue, &msg, 50); // 50ms timeout
        if (ret == BK_OK)
        {
            switch (msg.event)
            {
                case YUV_DISPLAY_EVENT_STOP:
                    LOGI("%s: received stop event\n", __func__);
                    goto exit;

                case YUV_DISPLAY_EVENT_FRAME_READY:
                    // Hardware rotation complete
                    yuv_display_rotation_complete(processing_frame);
                    processing_frame = NULL;
                    break;

                default:
                    break;
            }
        }

        // If no frame is being processed, try to get new frame
        if (processing_frame == NULL)
        {
            yuv_frame = frame_queue_v2_get_frame(IMAGE_YUV, CONSUMER_DECODER, 10);
            if (yuv_frame)
            {
                LOG_DEBUG("%s: got yuv frame, size:%dx%d, seq:%d\n",
                          __func__, yuv_frame->width, yuv_frame->height, yuv_frame->sequence);

                processing_frame = yuv_frame;
                ret = yuv_display_process_frame(yuv_frame);

                if (ret != BK_OK)
                {
                    LOGE("%s: process frame failed, ret:%d\n", __func__, ret);
                    frame_queue_v2_release_frame(IMAGE_YUV, CONSUMER_DECODER, yuv_frame);
                    processing_frame = NULL;
                }
                else if (!s_yuv_display->rotate_enabled ||
                         (s_yuv_display->rotate_angle != ROTATE_90 &&
                          s_yuv_display->rotate_angle != ROTATE_270))
                {
                    // No hardware rotation case, already released via display callback in process_frame
                    // Don't release here to avoid double free
                    processing_frame = NULL;
                }
                // Hardware rotation case, keep processing_frame and wait for rotation complete event
            }
        }
    }

exit:
    LOGI("%s: task exiting\n", __func__);

    // Cleanup resources
    // If there's pending input frame (waiting for rotation), release it
    if (processing_frame)
    {
        LOGW("%s: releasing pending input frame\n", __func__);
        frame_queue_v2_release_frame(IMAGE_YUV, CONSUMER_DECODER, processing_frame);
    }

    // If there's rotate output frame (may not displayed yet), release it
    if (s_yuv_display->rotate_output_frame)
    {
        LOGW("%s: releasing rotate output frame\n", __func__);
        yuv_display_rotate_frame_free_cb(s_yuv_display->rotate_output_frame);
        s_yuv_display->rotate_output_frame = NULL;
    }

    s_yuv_display->task_running = false;
    rtos_set_semaphore(&s_yuv_display->sem);
    rtos_delete_thread(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

bk_err_t yuv_display_open(db_device_info_t *info, int rotate_angle)
{
    bk_err_t ret = BK_OK;

    if (info == NULL)
    {
        LOGE("%s: invalid parameter, info is NULL\n", __func__);
        return BK_ERR_PARAM;
    }

    if (s_yuv_display != NULL)
    {
        LOGW("%s: task already started\n", __func__);
        return BK_OK;
    }

    LOGI("%s: opening YUV display, rotate_angle:%d\n", __func__, rotate_angle);

    // Allocate task structure
    s_yuv_display = (yuv_display_task_t *)os_malloc(sizeof(yuv_display_task_t));
    if (s_yuv_display == NULL)
    {
        LOGE("%s: malloc task failed\n", __func__);
        return BK_ERR_NO_MEM;
    }
    os_memset(s_yuv_display, 0, sizeof(yuv_display_task_t));

    // Configure rotation parameters
    s_yuv_display->rotate_angle = get_rotate_angle(rotate_angle);
    s_yuv_display->rotate_enabled = (s_yuv_display->rotate_angle != ROTATE_NONE);
    s_yuv_display->display_handle = info->display_ctlr_handle;

    if (s_yuv_display->display_handle == NULL)
    {
        LOGW("%s: display_handle is NULL, YUV will not be displayed\n", __func__);
    }

    LOGI("%s: rotate_angle:%d, rotate_enabled:%d, display_handle:%p\n", __func__,
         s_yuv_display->rotate_angle, s_yuv_display->rotate_enabled,
         s_yuv_display->display_handle);

    // Initialize hardware rotation module (only for 90/270 degree rotation)
    if (s_yuv_display->rotate_enabled &&
        (s_yuv_display->rotate_angle == ROTATE_90 || s_yuv_display->rotate_angle == ROTATE_270))
    {
        ret = bk_rott_driver_init();
        if (ret != BK_OK)
        {
            LOGE("%s: rott driver init failed, ret:%d\n", __func__, ret);
            goto error;
        }

        // Enable rotation complete interrupt
        bk_rott_int_enable(ROTATE_COMPLETE_INT, 1);
        bk_rott_isr_register(ROTATE_COMPLETE_INT, yuv_display_rotate_complete_cb);

        LOGI("%s: hardware rotation module initialized\n", __func__);
    }

    // Create semaphore
    ret = rtos_init_semaphore(&s_yuv_display->sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s: init semaphore failed, ret:%d\n", __func__, ret);
        goto error;
    }

    // Create message queue
    ret = rtos_init_queue(&s_yuv_display->queue, "yuv_disp_queue",
                          sizeof(yuv_display_msg_t), 10);
    if (ret != BK_OK)
    {
        LOGE("%s: init queue failed, ret:%d\n", __func__, ret);
        goto error;
    }

    // Register YUV consumer
    ret = frame_queue_v2_register_consumer(IMAGE_YUV, CONSUMER_DECODER);
    if (ret != BK_OK)
    {
        LOGE("%s: register YUV consumer failed, ret:%d\n", __func__, ret);
        goto error;
    }

    // Create processing task
    ret = rtos_create_thread(&s_yuv_display->task_handle,
                             BEKEN_DEFAULT_WORKER_PRIORITY,
                             "yuv_disp",
                             (beken_thread_function_t)yuv_display_task_main,
                             2048,
                             NULL);
    if (ret != BK_OK)
    {
        LOGE("%s: create thread failed, ret:%d\n", __func__, ret);
        frame_queue_v2_unregister_consumer(IMAGE_YUV, CONSUMER_DECODER);
        goto error;
    }

    // Wait for task to start
    rtos_get_semaphore(&s_yuv_display->sem, BEKEN_WAIT_FOREVER);

    LOGI("%s: YUV display opened successfully\n", __func__);
    return BK_OK;

error:
    if (s_yuv_display)
    {
        if (s_yuv_display->queue)
        {
            rtos_deinit_queue(&s_yuv_display->queue);
        }
        if (s_yuv_display->sem)
        {
            rtos_deinit_semaphore(&s_yuv_display->sem);
        }
        if (s_yuv_display->rotate_enabled &&
            (s_yuv_display->rotate_angle == ROTATE_90 || s_yuv_display->rotate_angle == ROTATE_270))
        {
            bk_rott_driver_deinit();
        }
        os_free(s_yuv_display);
        s_yuv_display = NULL;
    }

    LOGE("%s: failed to open YUV display\n", __func__);
    return ret;
}

bk_err_t yuv_display_close(void)
{
    bk_err_t ret = BK_OK;
    yuv_display_msg_t msg;

    if (s_yuv_display == NULL || !s_yuv_display->task_running)
    {
        LOGW("%s: task not running\n", __func__);
        return BK_OK;
    }

    LOGI("%s: closing YUV display\n", __func__);

    // Send stop message
    msg.event = YUV_DISPLAY_EVENT_STOP;
    msg.param = NULL;
    ret = rtos_push_to_queue(&s_yuv_display->queue, &msg, BEKEN_WAIT_FOREVER);
    if (ret != BK_OK)
    {
        LOGE("%s: send stop msg failed, ret:%d\n", __func__, ret);
    }

    // Wait for task to exit
    rtos_get_semaphore(&s_yuv_display->sem, BEKEN_WAIT_FOREVER);

    // Wait a short time to ensure display system completes all callbacks
    rtos_delay_milliseconds(100);

    // Clear remaining YUV frames in queue
    frame_buffer_t *remain_frame = NULL;
    while ((remain_frame = frame_queue_v2_get_frame(IMAGE_YUV, CONSUMER_DECODER, 0)) != NULL)
    {
        LOGW("%s: releasing remaining frame in queue\n", __func__);
        frame_queue_v2_release_frame(IMAGE_YUV, CONSUMER_DECODER, remain_frame);
    }

    // Unregister YUV consumer
    frame_queue_v2_unregister_consumer(IMAGE_YUV, CONSUMER_DECODER);

    // Cleanup resources
    if (s_yuv_display->queue)
    {
        rtos_deinit_queue(&s_yuv_display->queue);
    }

    if (s_yuv_display->sem)
    {
        rtos_deinit_semaphore(&s_yuv_display->sem);
    }

    // Stop hardware rotation module
    if (s_yuv_display->rotate_enabled &&
        (s_yuv_display->rotate_angle == ROTATE_90 || s_yuv_display->rotate_angle == ROTATE_270))
    {
        bk_rott_driver_deinit();
    }

    os_free(s_yuv_display);
    s_yuv_display = NULL;

    LOGI("%s: YUV display closed\n", __func__);
    return BK_OK;
}

bool yuv_display_is_running(void)
{
    return (s_yuv_display != NULL && s_yuv_display->task_running);
}

