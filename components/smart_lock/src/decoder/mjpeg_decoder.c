#include "decoder/decoder.h"
#include "frame/frame_que_v2.h"
#include "frame_buffer.h"
#include <components/bk_display.h>
#include <components/bk_video_pipeline/bk_video_pipeline.h>
#include "yuv_display/yuv_display.h"

#define TAG "db_dec"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

// Debug log switch, set to 1 to enable detailed logs, set to 0 to disable
#define MJPEG_DECODER_DEBUG_LOG 0

#if MJPEG_DECODER_DEBUG_LOG
#define LOG_DEBUG(...) LOGI(__VA_ARGS__)
#else
#define LOG_DEBUG(...) do {} while(0)
#endif

static db_device_info_t *s_mjpeg_decoder_info = NULL;

static bk_err_t jpeg_complete(bk_err_t result, frame_buffer_t *out_frame)
{
    bk_err_t ret = BK_OK;
    LOG_DEBUG("%s: result:%d, mjpeg_frame:%p, frame->frame:%p\n",
              __func__, result, out_frame, out_frame ? out_frame->frame : NULL);
    // V2: Decoder as consumer, release MJPEG frame
    frame_queue_v2_release_frame(IMAGE_MJPEG, CONSUMER_DECODER, out_frame);
    return ret;
}

static frame_buffer_t *jpeg_read(uint32_t timeout_ms)
{
    // V2: Decoder as consumer, get MJPEG frame
    frame_buffer_t *frame = frame_queue_v2_get_frame(IMAGE_MJPEG, CONSUMER_DECODER, timeout_ms);
    LOG_DEBUG("%s: timeout:%d, mjpeg_frame:%p, frame->frame:%p\n",
              __func__, timeout_ms, frame, frame ? frame->frame : NULL);
    return frame;
}

const jpeg_callback_t jpeg_cbs =
{
    .read = jpeg_read,
    .complete = jpeg_complete,
};

static bk_err_t display_frame_free_cb(void *frame)
{
    LOG_DEBUG("%s: frame:%p, frame->frame:%p\n", __func__, frame,
              frame ? ((frame_buffer_t *)frame)->frame : NULL);
    frame_buffer_display_free((frame_buffer_t *)frame);
    return BK_OK;
}

static frame_buffer_t *decode_malloc(uint32_t size)
{
    frame_buffer_t *frame = NULL;
    frame = frame_buffer_display_malloc(size);
    LOG_DEBUG("%s: size:%d, frame:%p, frame->frame:%p\n", __func__, size, frame,
              frame ? frame->frame : NULL);
    return frame;
}

static bk_err_t decode_free(frame_buffer_t *frame)
{
    LOG_DEBUG("%s: frame:%p, frame->frame:%p\n", __func__, frame,
              frame ? frame->frame : NULL);
    frame_buffer_display_free(frame);
    return BK_OK;
}

static bk_err_t decode_complete(dec_end_type_t format_type, bk_err_t result, frame_buffer_t *out_frame)
{
    bk_err_t ret = BK_OK;

    LOG_DEBUG("%s: type:%d, result:%d, out_frame:%p, frame->frame:%p\n",
              __func__, format_type, result, out_frame,
              out_frame ? out_frame->frame : NULL);

    if (out_frame == NULL)
    {
        LOG_DEBUG("%s: out_frame is NULL, return\n", __func__);
        return BK_OK;
    }

    if (result != BK_OK)
    {
        LOG_DEBUG("%s: result FAIL, call display_frame_free_cb directly\n", __func__);
        display_frame_free_cb(out_frame);
        return BK_OK;
    }

    if (format_type == HW_DEC_END)
    {
        if (s_mjpeg_decoder_info && s_mjpeg_decoder_info->display_ctlr_handle)
        {
            LOG_DEBUG("%s: HW_DEC_END, call bk_display_flush, handle:%p\n", __func__, s_mjpeg_decoder_info->display_ctlr_handle);
            ret = bk_display_flush(s_mjpeg_decoder_info->display_ctlr_handle, (void *)out_frame, display_frame_free_cb);
            if (ret != BK_OK)
            {
                LOG_DEBUG("%s: HW_DEC_END, bk_display_flush FAIL ret:%d, call display_frame_free_cb\n",
                          __func__, ret);
                display_frame_free_cb(out_frame);
            }
            else
            {
                LOG_DEBUG("%s: HW_DEC_END, bk_display_flush OK, callback will be called later\n", __func__);
            }
        }
        else
        {
            LOG_DEBUG("%s: HW_DEC_END, no display_handle, call display_frame_free_cb directly\n", __func__);
            display_frame_free_cb(out_frame);
        }
    }
    else if (format_type == SW_DEC_END)
    {
        if (s_mjpeg_decoder_info && s_mjpeg_decoder_info->display_ctlr_handle)
        {
            LOG_DEBUG("%s: SW_DEC_END, call bk_display_flush, handle:%p\n", __func__, s_mjpeg_decoder_info->display_ctlr_handle);
            ret = bk_display_flush(s_mjpeg_decoder_info->display_ctlr_handle, (void *)out_frame, display_frame_free_cb);
            if (ret != BK_OK)
            {
                LOG_DEBUG("%s: SW_DEC_END, bk_display_flush FAIL ret:%d, call display_frame_free_cb\n",
                          __func__, ret);
                display_frame_free_cb(out_frame);
            }
            else
            {
                LOG_DEBUG("%s: SW_DEC_END, bk_display_flush OK, callback will be called later\n", __func__);
            }
        }
        else
        {
            LOG_DEBUG("%s: SW_DEC_END, no display_handle, call display_frame_free_cb directly\n", __func__);
            display_frame_free_cb(out_frame);
        }
    }

    LOG_DEBUG("%s: complete, return ret:%d\n", __func__, ret);
    return BK_OK;
}

const decode_callback_t decode_cbs =
{
    .malloc = decode_malloc,
    .free = decode_free,
    .complete = decode_complete,
};

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

void mjpeg_decoder_reset(void)
{
    if (s_mjpeg_decoder_info && s_mjpeg_decoder_info->video_pipeline_handle)
    {
        bk_video_pipeline_reset_decode(s_mjpeg_decoder_info->video_pipeline_handle);
    }
}

int mjpeg_decode_open(db_device_info_t *info, media_rotate_mode_t rot, int angle)
{
    avdk_err_t ret = AVDK_ERR_INVAL;

    s_mjpeg_decoder_info = info;

    LOG_DEBUG("%s: cam_type:%d, rot:%d, angle:%d\n", __func__, info->cam_type, rot, angle);


    if (info->cam_type == DVP_CAMERA)
    {
        // DVP camera outputs both YUV and MJPEG/H264
        // No need to decode MJPEG, directly use YUV for rotation and display
        LOGI("%s: Camera outputs YUV+%s, use YUV directly for display\n",
             __func__, (info->transfer_format == IMAGE_MJPEG) ? "MJPEG" : "H264");

        // Start YUV display module (process YUV data)
        ret = yuv_display_open(info, angle);
        if (ret != BK_OK)
        {
            LOGE("%s: yuv_display_open failed, ret:%d\n", __func__, ret);
            return ret;
        }

        LOGI("%s: YUV display opened, angle:%d\n", __func__, angle);
        return AVDK_ERR_OK;
    }

    // UVC camera or DVP without YUV output, use pipeline decoder
    if (info->cam_type != UVC_CAMERA)
    {
        LOG_DEBUG("%s, do not need create mjpeg decoder\n", __func__);
        ret = AVDK_ERR_OK;
        return ret;
    }

    LOG_DEBUG("%s: register MJPEG consumer\n", __func__);
    frame_queue_v2_register_consumer(IMAGE_MJPEG, CONSUMER_DECODER);

    if (info->video_pipeline_handle == NULL)
    {
        bk_video_pipeline_config_t video_pipeline_config = {0};
        video_pipeline_config.jpeg_cbs = &jpeg_cbs;
        video_pipeline_config.decode_cbs = &decode_cbs;
        ret = bk_video_pipeline_new(&info->video_pipeline_handle, &video_pipeline_config);
        if (ret != BK_OK)
        {
            LOGE("%s, bk_video_pipeline_new fail\n", __func__);
            frame_queue_v2_unregister_consumer(IMAGE_MJPEG, CONSUMER_DECODER);
            return ret;
        }
    }

    bk_video_pipeline_decode_config_t decode_config = {0};

    decode_config.rotate_mode = rot;
    decode_config.rotate_angle = get_rotate_angle(angle);  //0,90,180,270

    ret = bk_video_pipeline_open_rotate(info->video_pipeline_handle, &decode_config);
    if (ret != BK_OK)
    {
        LOGE("%s, video_pipeline_handle open fail\n", __func__);
        frame_queue_v2_unregister_consumer(IMAGE_MJPEG, CONSUMER_DECODER);
        return ret;
    }

    LOG_DEBUG("%s: mjpeg decoder open success, pipeline_handle:%p\n",
              __func__, info->video_pipeline_handle);
    return ret;
}

int mjpeg_decode_close(db_device_info_t *info)
{
    avdk_err_t ret = AVDK_ERR_OK;

    LOG_DEBUG("%s: pipeline_handle:%p, info->handle:%p, cam_type:%d\n",
              __func__, info->video_pipeline_handle, info->handle, info->cam_type);

    // If DVP camera and YUV display module is running, stop it
    if (info->cam_type == DVP_CAMERA && yuv_display_is_running())
    {
        LOGI("%s: closing YUV display module\n", __func__);
        ret = yuv_display_close();
        if (ret != BK_OK)
        {
            LOGE("%s: yuv_display_close failed, ret:%d\n", __func__, ret);
        }
        LOG_DEBUG("%s: YUV display closed\n", __func__);
        s_mjpeg_decoder_info = NULL;
        return ret;
    }

    // Handle pipeline decoder close
    if (info->video_pipeline_handle == NULL)
    {
        LOGW("%s, mjpeg decoder already close\n", __func__);
        return ret;
    }

    ret = bk_video_pipeline_close_rotate(info->video_pipeline_handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s %d, video_pipeline_handle close fail\n", __func__, __LINE__);
    }

    if (info->handle == NULL)
    {
        LOG_DEBUG("%s: unregister MJPEG consumer\n", __func__);
        frame_queue_v2_unregister_consumer(IMAGE_MJPEG, CONSUMER_DECODER);
    }

    s_mjpeg_decoder_info = NULL;
    LOG_DEBUG("%s: mjpeg decoder close complete\n", __func__);
    return ret;
}
