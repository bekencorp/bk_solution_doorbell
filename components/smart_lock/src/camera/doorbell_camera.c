#include <components/bk_video_pipeline/bk_video_pipeline.h>
#include "doorbell_devices.h"
#include "doorbell_comm.h"
#include "frame_buffer.h"
#include "frame/frame_que_v2.h"
#include "camera/camera.h"
#include "decoder/decoder.h"

#define TAG "db-cam"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)


extern const jpeg_callback_t jpeg_cbs;
extern const decode_callback_t decode_cbs;

static frame_buffer_t *h264e_frame_malloc(uint32_t size)
{
    // V2版本：生产者申请帧
    return frame_queue_v2_malloc(IMAGE_H264, size);
}

static void h264e_frame_complete(frame_buffer_t *frame, int result)
{
    if (result != AVDK_ERR_OK)
    {
        // 编码失败，通过queue的cancel机制安全释放
        // 不能直接调用frame_buffer_free，否则会导致double free!
        frame_queue_v2_cancel(IMAGE_H264, frame);
    }
    else
    {
        // V2版本：编码成功，将帧放入ready队列供消费者使用
        frame_queue_v2_complete(IMAGE_H264, frame);
    }
}

static const bk_h264e_callback_t doorbell_h264e_cbs =
{
    .malloc = h264e_frame_malloc,
    .complete = h264e_frame_complete,
};

int doorbell_h264_encode_turn_on(db_device_info_t *info, camera_parameters_t *parameters)
{
    avdk_err_t ret = AVDK_ERR_INVAL;
    bk_video_pipeline_config_t video_pipeline_config = {0};
    bk_video_pipeline_h264e_config_t h264e_config = {0};

    frame_queue_v2_register_consumer(IMAGE_MJPEG, CONSUMER_DECODER);

    video_pipeline_config.jpeg_cbs = &jpeg_cbs;
    video_pipeline_config.decode_cbs = &decode_cbs;
    if (info->video_pipeline_handle == NULL)
    {
        ret = bk_video_pipeline_new(&info->video_pipeline_handle, &video_pipeline_config);
        if (ret != AVDK_ERR_OK)
        {
            frame_queue_v2_unregister_consumer(IMAGE_MJPEG, CONSUMER_DECODER);
            LOGE("%s bk_video_pipeline_new failed\n", __func__);
            return ret;
        }
    }

    if (parameters->rotate == 0)
    {
        h264e_config.sw_rotate_angle = ROTATE_NONE;
    }
    else if (parameters->rotate == 90)
    {
        h264e_config.sw_rotate_angle = ROTATE_90;
    }
    else if (parameters->rotate == 180)
    {
        h264e_config.sw_rotate_angle = ROTATE_180;
    }
    else if (parameters->rotate == 270)
    {
        h264e_config.sw_rotate_angle = ROTATE_270;
    }
    else
    {
        h264e_config.sw_rotate_angle = ROTATE_NONE;
    }

    h264e_config.width = parameters->width;
    h264e_config.height = parameters->height;
    h264e_config.fps = FPS30;
    h264e_config.h264e_cb = &doorbell_h264e_cbs;
    ret = bk_video_pipeline_open_h264e(info->video_pipeline_handle, &h264e_config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s bk_video_pipeline_open_h264e failed\n", __func__);
    }

    return ret;
}

int doorbell_camera_open(db_device_info_t *info, camera_parameters_t *parameters)
{
    avdk_err_t ret = AVDK_ERR_INVAL;
    LOGD("%s, id: %d, %d X %d, format: %d, Protocol: %d\n", __func__,
         parameters->id, parameters->width, parameters->height,
         parameters->format, parameters->protocol);

    if (parameters->format)// transfer h264
    {
        info->transfer_format = IMAGE_H264;
    }
    else
    {
        LOGE("doorbell not support transfer MJPEG stream, please use doorviewer project\n");
        return AVDK_ERR_UNSUPPORTED;
    }

    if (parameters->id == UVC_DEVICE_ID)
    {
        info->cam_type = UVC_CAMERA;
#ifdef CONFIG_USB_CAMERA
        info->handle = uvc_camera_turn_on(parameters);
#endif
    }
    else
    {
        info->cam_type = DVP_CAMERA;
#ifdef CONFIG_DVP_CAMERA
        info->handle = dvp_camera_turn_on(parameters);
#endif
    }

    if (info->handle == NULL)
    {
        LOGE("%s, %d fail\n", __func__, __LINE__);
        return ret;
    }
    else
    {
        ret = AVDK_ERR_OK;
    }

    if (info->cam_type == UVC_CAMERA)
    {
        ret = doorbell_h264_encode_turn_on(info, parameters);
        if (ret != AVDK_ERR_OK)
        {
            return ret;
        }
    }

    if (info->display_ctlr_handle != NULL)
    {
        LOGW("%s, there is a problem maybe cannot change rot_mode, please check!\n", __func__);
        ret = mjpeg_decode_open(info, HW_ROTATE, parameters->rotate); // TODO: 需要根据实际情况修改
    }

    LOGD("%s success\n", __func__);

    return ret;
}

int doorbell_camera_close(db_device_info_t *info)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (info->video_pipeline_handle)
    {
        ret = bk_video_pipeline_close_h264e(info->video_pipeline_handle);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s bk_video_pipeline_close_h264e failed\n", __func__);
            return AVDK_ERR_GENERIC;
        }
    }

    if (info->cam_type == UVC_CAMERA)
    {
#ifdef CONFIG_USB_CAMERA
        ret = uvc_camera_turn_off(info->handle);
#endif
    }
    else
    {
#ifdef CONFIG_DVP_CAMERA
        ret = dvp_camera_turn_off(info->handle);
#endif
    }

    if (info->lcd_device == NULL)
    {
        frame_queue_v2_unregister_consumer(IMAGE_MJPEG, CONSUMER_DECODER);
    }

    return ret;
}
