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


int doorbell_camera_open(db_device_info_t *info, camera_parameters_t *parameters)
{
    avdk_err_t ret = AVDK_ERR_INVAL;
    LOGD("%s, id: %d, %d X %d, format: %d, Protocol: %d\n", __func__,
         parameters->id, parameters->width, parameters->height,
         parameters->format, parameters->protocol);

    if (parameters->format == 0)// transfer h264
    {
        info->transfer_format = IMAGE_MJPEG;
    }
    else
    {
        LOGE("doorviewer not support transfer H264 stream, please use doorbell project\n");
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

    // 只有UVC输出的MJPEG才需要解码
    if (info->cam_type == UVC_CAMERA && info->display_ctlr_handle != NULL)
    {
        LOGW("%s, there is a problem maybe cannot change rot_mode, please check!\n", __func__);
        mjpeg_decode_open(info, HW_ROTATE, 90); // TODO: 需要根据实际情况修改
    }

    return ret;
}

int doorbell_camera_close(db_device_info_t *info)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (info == NULL || info->handle == NULL)
    {
        LOGW("%s, already turn off\n", __func__);
        return AVDK_ERR_OK;
    }

    // if (info->video_pipeline_handle)
    // {
    //     ret = bk_video_pipeline_close_h264e(info->video_pipeline_handle);
    //     if (ret != AVDK_ERR_OK)
    //     {
    //         LOGE("%s bk_video_pipeline_close_h264e failed\n", __func__);
    //         return AVDK_ERR_GENERIC;
    //     }
    // }

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

    return ret;
}
