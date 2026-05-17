

#include <components/bk_frame_buffer.h>

#include <components/usb_types.h>
#include <components/bk_uvc_camera.h>
#include <avdk_error.h>
#include <components/usbh_hub_multiple_classes_api.h>
#include "encode_frame_que.h"
#include "mds_img_manager.h"
#include "app_camera_types.h"
#include "devices_mgmt.h"


#define TAG "app_uvc"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

static beken_semaphore_t s_uvc_connect_sem = NULL;
static bk_uvc_ctlr_handle_t s_uvc_handle[UVC_PORT_MAX] = {NULL};

static frame_buffer_t *uvc_camera_frame_malloc(bk_image_format_t format, uint32_t size)
{
    return (frame_buffer_t *)bk_encoded_data_request();
}

static void uvc_camera_frame_complete(uint8_t port, bk_image_format_t format, frame_buffer_t *frame, int result)
{
    avdk_err_t ret = AVDK_ERR_INVAL;
    if (result == AVDK_ERR_OK)
    {
        frame->h264_type = port; // record the port id of the frame, temporary solution
        frame->fmt = PIXEL_FMT_JPEG;
        ret = bk_encoded_data_complete_request((uint8_t *)frame);
    }

    if (ret != AVDK_ERR_OK)
    {
        bk_encoded_complete_data_free_request((uint8_t *)frame);
    }
}

static frame_buffer_t *uvc_camera_frame_malloc_v2(bk_image_format_t format, uint32_t size)
{
    return encode_free_frame_que_pop();
}

static void uvc_camera_frame_complete_v2(uint8_t port, bk_image_format_t format, frame_buffer_t *frame, int result)
{
    if (result == AVDK_ERR_OK)
    {
        encode_ready_frame_que_push(frame);
    }
    else
    {
        encode_free_frame_que_push(frame);
    }
}

static void uvc_event_callback(uvc_state_t state, void *user_data)
{
    LOGI("%s, state:%d, user_data:%p\n", __func__, state, *((bk_uvc_ctlr_handle_t *)user_data));
}

static const bk_uvc_callback_t uvc_camera_cbs =
{
    .frame_malloc = uvc_camera_frame_malloc,
    .frame_complete = uvc_camera_frame_complete,
    .state_change_cb = uvc_event_callback,
    .user_data = &s_uvc_handle,
};

static const bk_uvc_callback_t uvc_camera_cbs_v2 =
{
    .frame_malloc = uvc_camera_frame_malloc_v2,
    .frame_complete = uvc_camera_frame_complete_v2,
    .state_change_cb = uvc_event_callback,
    .user_data = &s_uvc_handle,
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
    LOGI("%s, %d, frame_num:%d\n", __func__, __LINE__, frame_num);
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

    frame_num = uvc_device_param->all_frame.h265_frame_num;
    LOGI("%s, %d, frame_num:%d\n", __func__, __LINE__, frame_num);
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
    avdk_err_t ret = AVDK_ERR_OK;

    if (user_config->format == BK_IMAGE_FORMAT_H264 || user_config->format == BK_IMAGE_FORMAT_H265)
    {
        ret = bk_usbh_hub_port_check_device(user_config->port, USB_UVC_H26X_DEVICE, &uvc_port_info);
    }
    else
    {
        ret = bk_usbh_hub_port_check_device(user_config->port, USB_UVC_DEVICE, &uvc_port_info);
    }

    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d, bk_usbh_hub_port_check_device failed, retry get <USB_UVC_DEVICE>\n", __func__, __LINE__);
        rtos_delay_milliseconds(3000);
        ret = bk_usbh_hub_port_check_device(user_config->port, USB_UVC_DEVICE, &uvc_port_info);
    }

    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d, bk_usbh_hub_port_check_device failed, format:%d\n", __func__, __LINE__, user_config->format);
        return ret;
    }

    bk_uvc_device_brief_info_t *uvc_device_param = (bk_uvc_device_brief_info_t *)uvc_port_info->usb_device_param;
    if (uvc_device_param == NULL)
    {
        LOGE("%s, %d, uvc_device_param is NULL\n", __func__, __LINE__);
        return AVDK_ERR_INVAL;
    }

    LOGD("PORT:0x%x\r\n", user_config->port);
    LOGD("VID:0x%x\r\n", uvc_device_param->vendor_id);
    LOGD("PID:0x%x\r\n", uvc_device_param->product_id);
    LOGD("BCD:0x%x\r\n", uvc_device_param->device_bcd);

    switch (user_config->format)
    {
        case BK_IMAGE_FORMAT_YUV:
            ret = uvc_check_yuv_config(uvc_device_param, user_config);
            break;

        case BK_IMAGE_FORMAT_MJPEG:
            ret = uvc_check_mjpeg_config(uvc_device_param, user_config);
            break;

        case BK_IMAGE_FORMAT_H264:
            ret = uvc_check_h264_config(uvc_device_param, user_config);
            break;

        case BK_IMAGE_FORMAT_H265:
            ret = uvc_check_h265_config(uvc_device_param, user_config);
            break;

        default:
            LOGE("%s, please check usb output format:%d\r\n", __func__, user_config->format);
            break;
    }

    LOGI("%s, %d, ret:%d\r\n", __func__, __LINE__, ret);

    return ret;
}

/**
 * @brief UVC device connection callback
 * 
 * This callback handles UVC device connection and creates backup of device parameters.
 */
static void uvc_device_connect_callback(bk_usb_hub_port_info *port_info, void *arg)
{
    LOGI("%s: port:%d, UVC device connected\n", __func__, port_info->port_index);

    if (!port_info) {
        LOGE("%s: port_info is NULL\n", __func__);
        return;
    }

    beken_semaphore_t sem = (beken_semaphore_t)arg;
    if (sem)
    {
        rtos_set_semaphore(&sem);
    }
}

static avdk_err_t uvc_camera_power_on(uint32_t timeout)
{
    // Power on the camera device and check have connected already
    //uint8_t port = 1;
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_uvc_connect_sem == NULL)
    {
        ret = rtos_init_semaphore(&s_uvc_connect_sem, 1);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, %d, rtos_init_semaphore failed, timeout:%d\n", __func__, __LINE__, timeout);
            return ret;
        }
    }

    rtos_get_semaphore(&s_uvc_connect_sem, BEKEN_NO_WAIT);

    for (uint8_t i = 1; i <= UVC_PORT_MAX; i++)
    {
        LOGI("%s, %d, register connect callback for port:%d\n", __func__, __LINE__, i);
        bk_usbh_hub_port_register_connect_callback(i, USB_UVC_DEVICE, uvc_device_connect_callback, s_uvc_connect_sem);
        bk_usbh_hub_multiple_devices_power_on(USB_HOST_MODE, i, USB_UVC_DEVICE);
    }

    for (uint8_t i = 1; i <= UVC_PORT_MAX; i++)
    {
        LOGI("%s, %d, register connect callback for port:%d\n", __func__, __LINE__, i);
        bk_usbh_hub_port_register_connect_callback(i, USB_UVC_H26X_DEVICE, uvc_device_connect_callback, s_uvc_connect_sem);
        bk_usbh_hub_multiple_devices_power_on(USB_HOST_MODE, i, USB_UVC_H26X_DEVICE);
    }

    bk_usb_hub_port_info *port_info = NULL;
    bk_usb_hub_port_info *port_info_h26x = NULL;

    avdk_err_t ret1 = AVDK_ERR_UNSUPPORTED;
    avdk_err_t ret2 = AVDK_ERR_UNSUPPORTED;

    // Check if devices are already connected
    for (uint8_t i = 1; i <= UVC_PORT_MAX; i++)
    {
        ret1 = bk_usbh_hub_port_check_device(i, USB_UVC_DEVICE, &port_info);

        ret2 = bk_usbh_hub_port_check_device(i, USB_UVC_H26X_DEVICE, &port_info_h26x);

        if (ret1 == AVDK_ERR_OK || ret2 == AVDK_ERR_OK)
        {
            break;
        }
    }

    if (ret1 != AVDK_ERR_OK && ret2 != AVDK_ERR_OK)
    {
        ret = rtos_get_semaphore(&s_uvc_connect_sem, timeout);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, %d, rtos_get_semaphore failed, timeout:%d\n", __func__, __LINE__, timeout);
        }
    }

    return ret;
}

static avdk_err_t uvc_camera_power_off(void)
{
    for (uint8_t index = 0; index < UVC_PORT_MAX; index++)
    {
        if (s_uvc_handle[index] != NULL)
        {
            LOGW("%s, %d, uvc port:%d not closed\n", __func__, __LINE__, index + 1);
            return AVDK_ERR_GENERIC;
        }
    }
    for (uint8_t port = 1; port <= UVC_PORT_MAX; port++)
    {
        // Unregister the connection callback for this port and device
        bk_usbh_hub_port_register_connect_callback(port, USB_UVC_DEVICE, NULL, NULL);
        // Power down the specified device on this port
        bk_usbh_hub_multiple_devices_power_down(USB_HOST_MODE, port, USB_UVC_DEVICE);
        // Unregister the connection callback for this port and device
        bk_usbh_hub_port_register_connect_callback(port, USB_UVC_H26X_DEVICE, NULL, NULL);
        // Power down the specified device on this port
        bk_usbh_hub_multiple_devices_power_down(USB_HOST_MODE, port, USB_UVC_H26X_DEVICE);
    }

    if (s_uvc_connect_sem != NULL)
    {
        rtos_deinit_semaphore(&s_uvc_connect_sem);
        s_uvc_connect_sem = NULL;
    }

    return AVDK_ERR_OK;
}

bk_uvc_ctlr_handle_t uvc_camera_turn_on(bk_cam_uvc_config_t *config)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;
    bk_uvc_ctlr_handle_t handle = NULL;

    bk_image_format_t format = config->format;
    config->format = BK_IMAGE_FORMAT_MJPEG;
    if (config == NULL)
    {
        LOGE("%s: parameters is NULL\n", __func__);
        return NULL;
    }

    if (format == BK_IMAGE_FORMAT_H264)
    {
        ret = encode_frame_que_init();
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, %d, encode frame queue init failed\n", __func__, __LINE__);
            return NULL;
        }
    }

    // Power on the camera device and check have connected
    ret = uvc_camera_power_on(4000);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: uvc_camera_power_on failed\n", __func__);
        goto exit;
    }

    // suggest add delay to ensure the camera is stable connected
    // rtos_delay_milliseconds(3000);

    // Check the port info and input resolution/format uvc support or not
    // user should check the port info by yourself
#if 1
    ret = uvc_checkout_port_info(config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d: uvc_checkout_port_info failed\n", __func__, __LINE__);
        goto exit;
    }
#endif
    if (format == BK_IMAGE_FORMAT_MJPEG)
    {
        ret = bk_uvc_ctrl_new(&handle, &uvc_camera_cbs);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, %d: bk_uvc_camera_ctlr_new failed\n", __func__, __LINE__);
            goto exit;
        }
    }
    else
    {
        ret = bk_uvc_ctrl_new(&handle, &uvc_camera_cbs_v2);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, %d: bk_uvc_camera_ctlr_new failed\n", __func__, __LINE__);
            goto exit;
        }
    }

    ret = bk_uvc_init(handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d: bk_uvc_init failed\n", __func__, __LINE__);
        goto exit;
    }

    ret = bk_uvc_open(handle, config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d: bk_uvc_open failed\n", __func__, __LINE__);
        goto exit;
    }

    LOGD("%s open successful\n", __func__);

    return handle;

exit:
    if (handle != NULL)
    {
        bk_uvc_deinit(handle);
        bk_uvc_delete(handle);
    }
    uvc_camera_power_off();
    return NULL;
}

avdk_err_t uvc_camera_turn_off(uint8_t index)
{
    avdk_err_t ret = AVDK_ERR_OK;
    bk_uvc_ctlr_handle_t handle = s_uvc_handle[index];
    if (handle == NULL)
    {
        return ret;
    }

    ret = bk_uvc_close(handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_uvc_close failed\n", __func__);
        return ret;
    }

    ret = bk_uvc_deinit(handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_uvc_deinit failed\n", __func__);
        return ret;
    }

    ret = bk_uvc_delete(handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_uvc_delete failed\n", __func__);
        return ret;
    }

    s_uvc_handle[index] = NULL;

    uvc_camera_power_off();

    return ret;
}

avdk_err_t app_uvc_turn_on(camera_parameters_ext_t *parameters)
{
    avdk_err_t ret = AVDK_ERR_OK;
    uint8_t index = parameters->port - 1;

    if (s_uvc_handle[index] != NULL)
    {
        LOGE("uvc already open, please close first\n");
        return ret;
    }

    bk_cam_uvc_config_t config = MEDIA_UVC_MJPEG_864X480_30FPS_CONFIG();
    config.width = parameters->camera_width;
    config.height = parameters->camera_height;
    config.port = index + 1;
    config.format = BK_IMAGE_FORMAT_MJPEG;

    if (parameters->camera_out_format)
    {
        config.format = BK_IMAGE_FORMAT_H264;
    }

    LOGI("uvc open\n");

    bk_encoded_data_manager_init();

    s_uvc_handle[index] = uvc_camera_turn_on(&config);
    if (s_uvc_handle[index] == NULL)
    {
        LOGE("uvc open failed\n");
        ret = AVDK_ERR_INVAL;
        goto err;
    }

    app_uvc_device_t uvc_device = {
        .width = config.width,
        .height = config.height,
        .format = config.format,
    };

    ret = devices_mgmt_add_uvc_device(&uvc_device, index);

    if (ret != BK_OK)
    {
        LOGE("devices_mgmt_add_uvc_device failed\n");
        goto err;
    }

    ret = devices_mgmt_set_display_source(DISPLAY_STREAM_ID_PORT_0_UVC, NULL);

    if (ret != BK_OK)
    {
        LOGE("devices_mgmt_set_display_source failed\n");
        goto err;
    }

    return ret;

err:
    if (s_uvc_handle[index] != NULL)
    {
        (void)uvc_camera_turn_off(index);
        s_uvc_handle[index] = NULL;
    }
    return ret;
}

avdk_err_t app_uvc_turn_off(uint8_t port_id)
{
    avdk_err_t ret = AVDK_ERR_OK;
    if (port_id > UVC_PORT_MAX || port_id == 0)
    {
        LOGE("port_id:%d out of range, please check the port_id\n", port_id);
        return AVDK_ERR_INVAL;
    }
    ret = uvc_camera_turn_off(port_id - 1);// convert to index
    s_uvc_handle[port_id - 1] = NULL;
    return ret;
}