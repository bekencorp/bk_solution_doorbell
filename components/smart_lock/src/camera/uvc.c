#include "frame/frame_que_v2.h"
#include "doorbell_comm.h"
#include "camera/camera.h"
#include "decoder/decoder.h"
#include "frame_buffer.h"

#include <components/usb_types.h>
#include <components/bk_camera_ctlr.h>
#include <components/usbh_hub_multiple_classes_api.h>

#define TAG "db_uvc"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

static void uvc_camera_frame_complete(uint8_t port, image_format_t format, frame_buffer_t *frame, int result)
{
    if (result != AVDK_ERR_OK)
    {
        // UVC采集失败，通过queue的cancel机制安全释放
        frame_queue_v2_cancel(format, frame);
    }
    else
    {
        // V2版本：采集成功，将帧放入ready队列供消费者使用
        frame_queue_v2_complete(format, frame);
    }
}

static void uvc_event_callback(bk_usb_hub_port_info *port_info, void *arg, uvc_error_code_t code)
{
    LOGD("%s, code:%d\n", __func__, code);
    if (code == BK_UVC_DISCONNECT)
    {
        mjpeg_decoder_reset();
    }
}

static const bk_uvc_callback_t doorbell_uvc_cbs =
{
    .malloc = frame_queue_v2_malloc,
    .complete = uvc_camera_frame_complete,
    .uvc_event_callback = uvc_event_callback,
};

static avdk_err_t uvc_check_mjpeg_config(bk_uvc_device_brief_info_t *uvc_device_param, bk_cam_uvc_config_t *user_config)
{
    uint8_t frame_num = 0;
    uint8_t index = 0;
    uint8_t resolution_flag = false;
    uint8_t fps_flag = false;
    avdk_err_t ret = AVDK_ERR_UNSUPPORTED;

    frame_num = uvc_device_param->all_frame.mjpeg_frame_num;
    for (index = 0; index < frame_num; index++)
    {
        LOGD("MJPEG width:%d heigth:%d index:%d\r\n",
             uvc_device_param->all_frame.mjpeg_frame[index].width,
             uvc_device_param->all_frame.mjpeg_frame[index].height,
             uvc_device_param->all_frame.mjpeg_frame[index].index);

        if (uvc_device_param->all_frame.mjpeg_frame[index].width == user_config->width
            && uvc_device_param->all_frame.mjpeg_frame[index].height == user_config->height)
        {
            resolution_flag = true;
        }

        // iterate all support fps of current resolution
        for (int i = 0; i < uvc_device_param->all_frame.mjpeg_frame[index].fps_num; i++)
        {
            LOGD("MJPEG fps:%d\r\n", uvc_device_param->all_frame.mjpeg_frame[index].fps[i]);

            if (resolution_flag
                && uvc_device_param->all_frame.mjpeg_frame[index].fps[i] == user_config->fps)
            {
                fps_flag = true;
            }
        }

        if (resolution_flag)
        {
            // have adapt this resolution
            if (fps_flag == false)
            {
                user_config->fps = uvc_device_param->all_frame.mjpeg_frame[index].fps[0];
                fps_flag = true;
            }
            break;
        }
    }

    if (resolution_flag && fps_flag)
    {
        ret = AVDK_ERR_OK;
    }

    return ret;
}

static avdk_err_t uvc_check_yuv_config(bk_uvc_device_brief_info_t *uvc_device_param, bk_cam_uvc_config_t *user_config)
{
    uint8_t frame_num = 0;
    uint8_t index = 0;
    uint8_t resolution_flag = false;
    uint8_t fps_flag = false;
    avdk_err_t ret = AVDK_ERR_UNSUPPORTED;

    frame_num = uvc_device_param->all_frame.yuv_frame_num;
    for (index = 0; index < frame_num; index++)
    {
        LOGD("YUV width:%d heigth:%d index:%d\r\n",
             uvc_device_param->all_frame.yuv_frame[index].width,
             uvc_device_param->all_frame.yuv_frame[index].height,
             uvc_device_param->all_frame.yuv_frame[index].index);

        if (uvc_device_param->all_frame.yuv_frame[index].width == user_config->width
            && uvc_device_param->all_frame.yuv_frame[index].height == user_config->height)
        {
            resolution_flag = true;
        }

        for (int i = 0; i < uvc_device_param->all_frame.yuv_frame[index].fps_num; i++)
        {
            LOGD("YUV fps:%d\r\n", uvc_device_param->all_frame.yuv_frame[index].fps[i]);

            if (resolution_flag
                && uvc_device_param->all_frame.yuv_frame[index].fps[i] == user_config->fps)
            {
                fps_flag = true;
            }
        }

        if (resolution_flag)
        {
            // have adapt this resolution
            if (fps_flag == false)
            {
                user_config->fps = uvc_device_param->all_frame.yuv_frame[index].fps[0];
                fps_flag = true;
            }
            break;
        }
    }

    if (resolution_flag && fps_flag)
    {
        ret = AVDK_ERR_OK;
    }

    return ret;
}


static avdk_err_t uvc_check_h264_config(bk_uvc_device_brief_info_t *uvc_device_param, bk_cam_uvc_config_t *user_config)
{
    uint8_t frame_num = 0;
    uint8_t index = 0;
    uint8_t resolution_flag = false;
    uint8_t fps_flag = false;
    avdk_err_t ret = AVDK_ERR_UNSUPPORTED;

    frame_num = uvc_device_param->all_frame.h264_frame_num;
    for (index = 0; index < frame_num; index++)
    {
        LOGD("H264 width:%d heigth:%d index:%d\r\n",
             uvc_device_param->all_frame.h264_frame[index].width,
             uvc_device_param->all_frame.h264_frame[index].height,
             uvc_device_param->all_frame.h264_frame[index].index);

        if (uvc_device_param->all_frame.h264_frame[index].width == user_config->width
            && uvc_device_param->all_frame.h264_frame[index].height == user_config->height)
        {
            resolution_flag = true;
        }

        // iterate all support fps of current resolution
        for (int i = 0; i < uvc_device_param->all_frame.h264_frame[index].fps_num; i++)
        {
            LOGD("H264 fps:%d\r\n", uvc_device_param->all_frame.h264_frame[index].fps[i]);

            if (resolution_flag
                && uvc_device_param->all_frame.h264_frame[index].fps[i] == user_config->fps)
            {
                fps_flag = true;
            }
        }

        if (resolution_flag)
        {
            // have adapt this resolution
            if (fps_flag == false)
            {
                user_config->fps = uvc_device_param->all_frame.h264_frame[index].fps[0];
                fps_flag = true;
            }
            break;
        }
    }

    if (resolution_flag && fps_flag)
    {
        ret = AVDK_ERR_OK;
    }

    return ret;

}

static avdk_err_t uvc_check_h265_config(bk_uvc_device_brief_info_t *uvc_device_param, bk_cam_uvc_config_t *user_config)
{
    uint8_t frame_num = 0;
    uint8_t index = 0;
    uint8_t resolution_flag = false;
    uint8_t fps_flag = false;
    avdk_err_t ret = AVDK_ERR_UNSUPPORTED;

    for (index = 0; index < frame_num; index++)
    {
        LOGD("H265 width:%d heigth:%d index:%d\r\n",
             uvc_device_param->all_frame.h265_frame[index].width,
             uvc_device_param->all_frame.h265_frame[index].height,
             uvc_device_param->all_frame.h265_frame[index].index);

        if (uvc_device_param->all_frame.h265_frame[index].width == user_config->width
            && uvc_device_param->all_frame.h265_frame[index].height == user_config->height)
        {
            resolution_flag = true;
        }

        // iterate all support fps of current resolution
        for (int i = 0; i < uvc_device_param->all_frame.h265_frame[index].fps_num; i++)
        {
            LOGD("H265 fps:%d\r\n", uvc_device_param->all_frame.h265_frame[index].fps[i]);

            if (resolution_flag
                && uvc_device_param->all_frame.h265_frame[index].fps[i] == user_config->fps)
            {
                fps_flag = true;
            }
        }

        if (resolution_flag)
        {
            // have adapt this resolution
            if (fps_flag == false)
            {
                user_config->fps = uvc_device_param->all_frame.h265_frame[index].fps[0];
                fps_flag = true;
            }
            break;
        }
    }

    if (resolution_flag && fps_flag)
    {
        ret = AVDK_ERR_OK;
    }

    return ret;
}


static avdk_err_t uvc_checkout_port_info(bk_cam_uvc_config_t *user_config)
{
    bk_usb_hub_port_info *uvc_port_info = NULL;
    avdk_err_t ret = bk_usbh_hub_port_check_device(user_config->port, USB_UVC_DEVICE, &uvc_port_info);
    if (uvc_port_info == NULL || ret != AVDK_ERR_OK)
    {
        LOGE("%s: uvc_port_info is NULL\n", __func__);
        return AVDK_ERR_INVAL;
    }

    bk_uvc_device_brief_info_t *uvc_device_param = (bk_uvc_device_brief_info_t *)uvc_port_info->usb_device_param;

    LOGD("PORT:0x%x\r\n", user_config->port);
    LOGD("VID:0x%x\r\n", uvc_device_param->vendor_id);
    LOGD("PID:0x%x\r\n", uvc_device_param->product_id);
    LOGD("BCD:0x%x\r\n", uvc_device_param->device_bcd);

    switch (user_config->img_format)
    {
        case IMAGE_YUV:
            ret = uvc_check_yuv_config(uvc_device_param, user_config);
            break;

        case IMAGE_MJPEG:
            ret = uvc_check_mjpeg_config(uvc_device_param, user_config);
            break;

        case IMAGE_H264:
            ret = uvc_check_h264_config(uvc_device_param, user_config);
            break;

        case IMAGE_H265:
            ret = uvc_check_h265_config(uvc_device_param, user_config);
            break;

        default:
            LOGE("%s, please check usb output format:%d\r\n", __func__, user_config->img_format);
            break;
    }

    LOGD("%s, %d\r\n", __func__, __LINE__);

    return ret;
}

static void uvc_connect_successful_callback(bk_usb_hub_port_info *port_info, void *arg)
{
    LOGI("%s, %d\n", __func__, __LINE__);
    beken_semaphore_t sem = (beken_semaphore_t)arg;
    rtos_set_semaphore(&sem);
}

static avdk_err_t uvc_camera_power_on(uint32_t timeout)
{
    // Power on the camera device and check have connected already
    uint8_t port = 1;
    beken_semaphore_t uvc_connect_sem = NULL;
    avdk_err_t ret = rtos_init_semaphore(&uvc_connect_sem, 1);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d\n", __func__, __LINE__);
        return ret;
    }

    bk_usbh_hub_port_register_connect_callback(port, USB_UVC_DEVICE, uvc_connect_successful_callback, uvc_connect_sem);
    bk_usbh_hub_multiple_devices_power_on(USB_HOST_MODE, port, USB_UVC_DEVICE);

    bk_usb_hub_port_info *port_info = NULL;  ///< Array of USB hub port information

    // After power-on is completed, check if connection is successful
    ret = bk_usbh_hub_port_check_device(port, USB_UVC_DEVICE, &port_info);
    // If not checked successfully, wait for the connection success callback
    if (ret != AVDK_ERR_OK)
    {
        ret = rtos_get_semaphore(&uvc_connect_sem, timeout);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, %d, timeout:%d\n", __func__, __LINE__, timeout);
        }
    }

    rtos_deinit_semaphore(&uvc_connect_sem);

    return ret;
}

static avdk_err_t uvc_camera_power_off(void)
{
    uint8_t port = 1;
    bk_usbh_hub_port_register_connect_callback(port, USB_UVC_DEVICE, NULL, NULL);
    bk_usbh_hub_multiple_devices_power_down(USB_HOST_MODE, port, USB_UVC_DEVICE);

    return AVDK_ERR_OK;
}

bk_camera_ctlr_handle_t uvc_camera_turn_on(camera_parameters_t *parameters)
{
    bk_err_t ret = BK_FAIL;
    bk_camera_ctlr_handle_t handle = NULL;

    if (parameters == NULL)
    {
        LOGE("%s: parameters is NULL\n", __func__);
        return NULL;
    }

    // Power on the camera device and check have connected
    ret = uvc_camera_power_on(4000);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: uvc_camera_power_on failed\n", __func__);
        goto exit;
    }

    bk_uvc_ctlr_config_t uvc_ctlr_config =
    {
        .config = BK_UVC_864X480_30FPS_MJPEG_CONFIG(),
        .cbs = &doorbell_uvc_cbs,
    };

    uvc_ctlr_config.config.width = parameters->width;
    uvc_ctlr_config.config.height = parameters->height;

    // Check the port info and input resolution/format uvc support or not
    ret = uvc_checkout_port_info(&uvc_ctlr_config.config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d: uvc_checkout_port_info failed\n", __func__, __LINE__);
        goto exit;
    }

    ret = bk_camera_uvc_ctlr_new(&handle, &uvc_ctlr_config);
    if (ret != BK_OK)
    {
        LOGE("%s, %d: bk_camera_uvc_ctlr_new failed\n", __func__, __LINE__);
        goto exit;
    }

    ret = bk_camera_open(handle);
    if (ret != BK_OK)
    {
        LOGE("%s, %d: bk_camera_open failed\n", __func__, __LINE__);
        bk_camera_delete(handle);
        handle = NULL;
        goto exit;
    }

    LOGD("%s open successful\n", __func__);

    return handle;

exit:
    uvc_camera_power_off();
    return NULL;
}

avdk_err_t uvc_camera_turn_off(bk_camera_ctlr_handle_t handle)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (handle == NULL)
    {
        return ret;
    }

    ret = bk_camera_close(handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_camera_close failed\n", __func__);
        return ret;
    }

    ret = bk_camera_delete(handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_camera_delete failed\n", __func__);
        return ret;
    }

    uvc_camera_power_off();

    return ret;
}
