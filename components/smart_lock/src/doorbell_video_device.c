#include <os/mem.h>
#include "frame_buffer.h"
#include "wifi_transfer.h"
#include "doorbell_comm.h"
#include "doorbell_transmission.h"
#include "doorbell_cmd.h"
#include "doorbell_frame_que.h"
#include "doorbell_video_device.h"
#include "doorbell_cs2_service.h"
#include "components/bk_video_pipeline/bk_video_pipeline.h"
#include <components/usbh_hub_multiple_classes_api.h>
#include <driver/flash.h>

#define TAG "db-video"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define GPIO_INVALID_ID           (0xFF)
#ifdef CONFIG_DVP_CTRL_POWER_GPIO_ID
#define DVP_POWER_GPIO_ID CONFIG_DVP_CTRL_POWER_GPIO_ID
#else
#define DVP_POWER_GPIO_ID GPIO_INVALID_ID
#endif

typedef struct
{
    image_format_t transfer_format;
    beken_semaphore_t uvc_connect_semaphore;    ///< Semaphore for UVC connection synchronization
    beken_mutex_t uvc_mutex;                    ///< Mutex for protecting shared resources
    bk_usb_hub_port_info *port_info;  ///< Array of USB hub port information
    bk_camera_ctlr_handle_t handle;
    bk_video_pipeline_handle_t video_pipeline_handle;
    media_transfer_cb_t *video_transfer_cb;
} doorbell_video_info_t;

static doorbell_video_info_t *s_db_video_info = NULL;

static avdk_err_t doorbell_video_info_init(void)
{
    avdk_err_t ret = AVDK_ERR_OK;
    if (s_db_video_info == NULL)
    {
        s_db_video_info = (doorbell_video_info_t *)os_malloc(sizeof(doorbell_video_info_t));
        if (s_db_video_info == NULL)
        {
            LOGE("%s: s_db_video_info is NULL\n", __func__);
            return AVDK_ERR_NOMEM;
        }
        memset(s_db_video_info, 0, sizeof(doorbell_video_info_t));

        if (rtos_init_semaphore(&s_db_video_info->uvc_connect_semaphore, 1) != AVDK_ERR_OK)
        {
            LOGE("%s: init semaphore failed\n", __func__);
            os_free(s_db_video_info);
            s_db_video_info = NULL;
            return AVDK_ERR_NOMEM;
        }

        if (rtos_init_mutex(&s_db_video_info->uvc_mutex) != AVDK_ERR_OK)
        {
            LOGE("%s: init mutex failed\n", __func__);
            rtos_deinit_semaphore(&s_db_video_info->uvc_connect_semaphore);
            os_free(s_db_video_info);
            s_db_video_info = NULL;
            return AVDK_ERR_NOMEM;
        }
    }
    return ret;
}

static avdk_err_t doorbell_video_info_deinit(void)
{
    avdk_err_t ret = AVDK_ERR_OK;
    if (s_db_video_info != NULL)
    {
        rtos_deinit_mutex(&s_db_video_info->uvc_mutex);
        rtos_deinit_semaphore(&s_db_video_info->uvc_connect_semaphore);
        os_free(s_db_video_info);
        s_db_video_info = NULL;
    }
    return ret;
}

static frame_buffer_t *camera_frame_malloc(image_format_t format, uint32_t size)
{
    return frame_queue_malloc(format, size);
}

static void uvc_camera_frame_complete(uint8_t port, image_format_t format, frame_buffer_t *frame, int result)
{
    if (result != AVDK_ERR_OK)
    {
        frame_queue_free(format, frame);
    }
    else
    {
        frame_queue_complete(format, frame);
    }
}

static void dvp_camera_frame_complete(image_format_t format, frame_buffer_t *frame, int result)
{
    if (result != AVDK_ERR_OK)
    {
        frame_queue_free(format, frame);
    }
    else
    {
        frame_queue_complete(format, frame);
    }
}

static void doorbell_uvc_event_callback(bk_usb_hub_port_info *port_info,void *arg, uvc_error_code_t code)
{
    LOGD("%s, code:%d\n", __func__, code);

    if (code == BK_UVC_DISCONNECT && s_db_video_info->video_pipeline_handle)
    {
        bk_video_pipeline_reset_decode(s_db_video_info->video_pipeline_handle);
    }
}

static const bk_dvp_callback_t doorbell_dvp_cbs = {
    .malloc = camera_frame_malloc,
    .complete = dvp_camera_frame_complete,
};

static const bk_uvc_callback_t doorbell_uvc_cbs = {
    .malloc = camera_frame_malloc,
    .complete = uvc_camera_frame_complete,
    .uvc_event_callback = doorbell_uvc_event_callback,
};

static void doorbell_send_flash_op_state_callback(uint32_t state)
{
    if (s_db_video_info == NULL)
    {
        return;
    }

    if (state)
    {
        bk_camera_suspend(s_db_video_info->handle);
    }
    else
    {
        bk_camera_resume(s_db_video_info->handle);
    }
}

static avdk_err_t uvc_checkout_port_info(doorbell_video_info_t *info, bk_cam_uvc_config_t *user_config)
{
    if (info == NULL)
    {
        LOGE("%s: info is NULL\n", __func__);
        return AVDK_ERR_INVAL;
    }

    if (user_config == NULL)
    {
        LOGE("%s: config is NULL\n", __func__);
        return AVDK_ERR_INVAL;
    }

    // 需要注意在检查的过程中可能会存在uvc突然断开，导致port_info失效， uvc_test_info_t不可用，如果继续访问其成员变量可能导致访问空指针
    // 所以需要加锁保护
    rtos_lock_mutex(&info->uvc_mutex);

    uint8_t frame_num = 0;
    uint8_t index = 0;
    uint8_t resolution_flag = false;
    uint8_t fps_flag = false;
    uint8_t format_flag = false;

    bk_usb_hub_port_info *uvc_port_info = info->port_info;
    if (uvc_port_info == NULL)
    {
        rtos_unlock_mutex(&info->uvc_mutex);
        LOGE("%s: uvc_port_info is NULL\n", __func__);
        return AVDK_ERR_INVAL;
    }

    LOGD("%s, %d, port:%d, format:%d, W*H:%d*%d\r\n", __func__, __LINE__, user_config->port, user_config->img_format,
        user_config->width, user_config->height);

    bk_uvc_device_brief_info_t *uvc_device_param = (bk_uvc_device_brief_info_t *)uvc_port_info->usb_device_param;
    bk_uvc_config_t *uvc_device_param_config = (bk_uvc_config_t *)uvc_port_info->usb_device_param_config;

    LOGD("PORT:0x%x\r\n", user_config->port);
    LOGD("VID:0x%x\r\n", uvc_device_param->vendor_id);
    LOGD("PID:0x%x\r\n", uvc_device_param->product_id);
    LOGD("BCD:0x%x\r\n", uvc_device_param->device_bcd);

    switch (user_config->img_format)
    {
        case IMAGE_YUV:
            frame_num = uvc_device_param->all_frame.yuv_frame_num;
            for (index = 0; index < frame_num; index++)
            {
                format_flag = true; // uvc support yuv format
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
            break;

        case IMAGE_MJPEG:
            frame_num = uvc_device_param->all_frame.mjpeg_frame_num;
            for (index = 0; index < frame_num; index++)
            {
                format_flag = true; // uvc support mjpeg format
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
            break;

        case IMAGE_H264:
            frame_num = uvc_device_param->all_frame.h264_frame_num;
            for (index = 0; index < frame_num; index++)
            {
                format_flag = true; // uvc support h264 format
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
            break;

        case IMAGE_H265:
            uvc_device_param_config->format_index = uvc_device_param->format_index.h265_format_index;
            frame_num = uvc_device_param->all_frame.h265_frame_num;
            for (index = 0; index < frame_num; index++)
            {
                format_flag = true; // uvc support h265 format
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
            break;

        default:
            LOGE("%s, please check usb output format:%d\r\n", __func__, user_config->img_format);
            break;
    }

    rtos_unlock_mutex(&info->uvc_mutex);

    if (format_flag == false)
    {
        LOGE("%s, not support this format:%d\r\n", __func__, user_config->img_format);
        return AVDK_ERR_UNSUPPORTED;
    }

    if (resolution_flag == false)
    {
        LOGE("%s, not support this resolution:%dX%d\r\n", __func__, user_config->width, user_config->height);
        return AVDK_ERR_UNSUPPORTED;
    }

    LOGD("%s, %d\r\n", __func__, __LINE__);

    return AVDK_ERR_OK;
}

static void uvc_connect_successful_callback(bk_usb_hub_port_info *port_info, void *arg)
{
    LOGI("%s, %d\n", __func__, __LINE__);
    doorbell_video_info_t *s_db_video_info = (doorbell_video_info_t *)arg;
    if (s_db_video_info == NULL)
        return;
    rtos_lock_mutex(&s_db_video_info->uvc_mutex);
    s_db_video_info->port_info = port_info;
    rtos_set_semaphore(&s_db_video_info->uvc_connect_semaphore);
    rtos_unlock_mutex(&s_db_video_info->uvc_mutex);
}

static void uvc_disconnect_callback(bk_usb_hub_port_info *port_info, void *arg)
{
    LOGI("%s, %d\n", __func__, __LINE__);
    doorbell_video_info_t *s_db_video_info = (doorbell_video_info_t *)arg;
    if (s_db_video_info == NULL)
        return;
    rtos_lock_mutex(&s_db_video_info->uvc_mutex);
    s_db_video_info->port_info = NULL;
    rtos_unlock_mutex(&s_db_video_info->uvc_mutex);
}

static avdk_err_t uvc_camera_power_on_handle(doorbell_video_info_t *info, uint32_t timeout)
{
    // Power on the camera device and check have connected already
    uint8_t port = 1;
    bk_usbh_hub_port_register_connect_callback(port, USB_UVC_DEVICE, uvc_connect_successful_callback, info);
    bk_usbh_hub_port_register_disconnect_callback(port, USB_UVC_DEVICE, uvc_disconnect_callback, info);
    bk_usbh_hub_multiple_devices_power_on(USB_HOST_MODE, port, USB_UVC_DEVICE);

    // After power-on is completed, check if connection is successful
    avdk_err_t ret = bk_usbh_hub_port_check_device(port, USB_UVC_DEVICE, &info->port_info);

    // If not checked successfully, wait for the connection success callback
    if (ret != AVDK_ERR_OK)
    {
        ret = rtos_get_semaphore(&info->uvc_connect_semaphore, timeout);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, %d, timeout:%d\n", __func__, __LINE__, timeout);
        }
    }

    return ret;
}

static avdk_err_t uvc_camera_power_off_handle(doorbell_video_info_t *info)
{
    // Before power-off, check if all ports are closed
    uint8_t port = 1;
    if (info == NULL)
    {
        LOGE("%s, %d: info is NULL\n", __func__, __LINE__);
        return AVDK_ERR_INVAL;
    }

    if (info->handle != NULL)
    {
        LOGW("%s, %d: handle not closed\n", __func__, __LINE__);
        return AVDK_ERR_BUSY;
    }

    rtos_get_semaphore(&info->uvc_connect_semaphore, BEKEN_NO_WAIT);
    bk_usbh_hub_port_register_connect_callback(port, USB_UVC_DEVICE, NULL, NULL);
    bk_usbh_hub_port_register_disconnect_callback(port, USB_UVC_DEVICE, NULL, NULL);
    bk_usbh_hub_multiple_devices_power_down(USB_HOST_MODE, port, USB_UVC_DEVICE);
    info->port_info = NULL;

    return AVDK_ERR_OK;
}

int doorbell_uvc_camera_turn_on(camera_parameters_t *parameters)
{
    bk_err_t ret = BK_FAIL;

    if (parameters == NULL)
    {
        LOGE("%s: parameters is NULL\n", __func__);
        return BK_FAIL;
    }

    if (s_db_video_info->handle != NULL)
    {
        LOGW("%s, uvc camera have been already opened!\n", __func__);
        return ret;
    }


    ret = doorbell_video_info_init();
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: doorbell_video_info_init failed\n", __func__);
        return ret;
    }

    if (parameters->format == 0) // wifi transfer format 0/1:mjpeg/h264
    {
        s_db_video_info->transfer_format = IMAGE_MJPEG;
    }
    else
    {
        s_db_video_info->transfer_format = IMAGE_H264;
    }

    // Power on the camera device and check have connected
    ret = uvc_camera_power_on_handle(s_db_video_info, 4000);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: uvc_camera_power_on_handle failed\n", __func__);
        goto exit;
    }

    bk_uvc_ctlr_config_t uvc_ctlr_config = {
        .config = BK_UVC_864X480_30FPS_MJPEG_CONFIG(),
        .cbs = &doorbell_uvc_cbs,
    };

    uvc_ctlr_config.config.width = parameters->width;
    uvc_ctlr_config.config.height = parameters->height;

        // Check the port info and input resolution/format uvc support or not
    ret = uvc_checkout_port_info(s_db_video_info, &uvc_ctlr_config.config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d: uvc_checkout_port_info failed\n", __func__, __LINE__);
        goto exit;
    }

    ret = bk_camera_uvc_ctlr_new(&s_db_video_info->handle, &uvc_ctlr_config);
    if (ret != BK_OK)
    {
        LOGE("%s, %d: bk_camera_uvc_ctlr_new failed\n", __func__, __LINE__);
        goto exit;
    }
    ret = bk_camera_open(s_db_video_info->handle);
    if (ret != BK_OK)
    {
        LOGE("%s, %d: bk_camera_open failed\n", __func__, __LINE__);
        bk_camera_delete(s_db_video_info->handle);
        s_db_video_info->handle = NULL;
        goto exit;
    }

#if CONFIG_FLASH
    /*while camera wroking, other user erase/write flash will influen the camera data,
    so we need to register a callback to notify the camera state, and drop error frame
    as much as possible */
    mb_flash_register_op_camera_notify(doorbell_send_flash_op_state_callback);
#endif

    LOGD("%s open successful\n", __func__);

    return ret;

exit:
    uvc_camera_power_off_handle(s_db_video_info);
    return ret;
}

int doorbell_dvp_camera_turn_on(camera_parameters_t *parameters)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (parameters == NULL)
    {
        LOGE("doorbell_uvc_camera_turn_on: parameters is NULL");
        return BK_FAIL;
    }

    if (s_db_video_info->handle != NULL)
    {
        LOGW("%s, dvp camera have been already opened!\n", __func__);
        return ret;
    }

    ret = doorbell_video_info_init();
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: doorbell_video_info_init failed\n", __func__);
        return ret;
    }

    // power on dvp
    if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
    {
        GPIO_UP(DVP_POWER_GPIO_ID);
    }

    bk_dvp_config_t dvp_config = BK_DVP_864X480_30FPS_MJPEG_CONFIG();
    if (parameters->format == 0) // wifi transfer format 0/1:mjpeg/h264
    {
        dvp_config.img_format = IMAGE_YUV | IMAGE_MJPEG;
        s_db_video_info->transfer_format = IMAGE_MJPEG;
    }
    else
    {
        dvp_config.img_format = IMAGE_YUV | IMAGE_H264;
        s_db_video_info->transfer_format = IMAGE_H264;
    }

    dvp_config.width = parameters->width;
    dvp_config.height = parameters->height;

    bk_dvp_ctlr_config_t dvp_ctlr_config = {
        .config = dvp_config,
        .cbs = &doorbell_dvp_cbs,
    };

    ret = bk_camera_dvp_ctlr_new(&s_db_video_info->handle, &dvp_ctlr_config);
    if (ret == BK_OK)
    {
        ret = bk_camera_open(s_db_video_info->handle);
        if (ret != BK_OK)
        {
            LOGE("%s bk_camera_dvp_ctlr_open failed\n", __func__);
            bk_camera_delete(s_db_video_info->handle);
            s_db_video_info->handle = NULL;
        }
        else
        {
#if CONFIG_FLASH
            /*while camera wroking, other user erase/write flash will influen the camera data,
            so we need to register a callback to notify the camera state, and drop error frame
            as much as possible */
            mb_flash_register_op_camera_notify(doorbell_send_flash_op_state_callback);
#endif

            LOGD("%s bk_camera_dvp_ctlr_open successful\n", __func__);
        }
    }
    else
    {
        LOGE("%s bk_camera_dvp_ctlr_new failed\n", __func__);
    }

    return ret;
}

bk_err_t doorbell_camera_device_turn_off(void)
{
    bk_err_t ret = BK_OK;

    if (s_db_video_info == NULL)
    {
        LOGE("%s: s_db_video_info is NULL\n", __func__);
        return BK_FAIL;
    }

    if (s_db_video_info->handle == NULL)
    {
        LOGE("%s: camera_handle is NULL\n", __func__);
        return ret;
    }

    ret = bk_camera_close(s_db_video_info->handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_camera_close failed\n", __func__);
        return ret;
    }

    ret = bk_camera_delete(s_db_video_info->handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_camera_delete failed\n", __func__);
        return ret;
    }
    else
    {
#if CONFIG_FLASH
        mb_flash_unregister_op_camera_notify();
#endif
        LOGD("%s: bk_camera_delete successful\n", __func__);
        s_db_video_info->handle = NULL;
    }

    if (s_db_video_info->port_info)
    {
        uvc_camera_power_off_handle(s_db_video_info);
    }

    // power off dvp
    if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
    {
        GPIO_DOWN(DVP_POWER_GPIO_ID);
    }

    return BK_OK;
}

int doorbell_set_h264_encode_handle(bk_video_pipeline_handle_t handle)
{
    if (s_db_video_info == NULL)
    {
        LOGE("%s: handle is NULL\n", __func__);
        return BK_FAIL;
    }

    s_db_video_info->video_pipeline_handle = handle;

    return BK_OK;
}

int doorbell_video_transfer_turn_on(void)
{
    int ret = BK_FAIL;

    if (s_db_video_info == NULL)
    {
        LOGE("%s: s_db_video_info is NULL\n", __func__);
        return BK_FAIL;
    }

    if (s_db_video_info->handle == NULL)
    {
        LOGE("%s: camera not open!\n", __func__);
        return BK_FAIL;
    }

    if (s_db_video_info->video_transfer_cb)
    {
        ret = bk_wifi_transfer_frame_open(s_db_video_info->video_transfer_cb, s_db_video_info->transfer_format);
    }

    if (ret != BK_OK)
    {
        LOGE("%s failed\n", __func__);
    }
    else
    {
        LOGD("%s success\n", __func__);
    }

    return ret;
}

int doorbell_video_transfer_turn_off(void)
{
    int ret = BK_OK;
    if (s_db_video_info == NULL)
    {
        LOGE("%s: s_db_video_info is NULL\n", __func__);
        return ret;
    }

    if (s_db_video_info->handle == NULL)
    {
        LOGE("%s: camera not open!\n", __func__);
        return ret;
    }

    ret = bk_wifi_transfer_frame_close();

#if (CONFIG_INTEGRATION_DOORBELL_CS2)
    doorbell_cs2_img_timer_deinit();
#endif

    if (ret != BK_OK)
    {
        LOGE("%s failed\n", __func__);
    }
    else
    {
        LOGD("%s success\n", __func__);
    }

    return ret;
}
int doorbell_get_supported_camera_devices(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb)
{
    db_evt_head_t *evt = os_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
    char *p = (char *)(evt + 1);

    evt->opcode = opcode;
    evt->status = EVT_STATUS_OK;
    evt->flags = EVT_FLAGS_CONTINUE;

    LOGD("DBCMD_GET_CAMERA_SUPPORTED_DEVICES\n");

#if (CAMERA_DEVICES_REPORT == BK_TRUE)

    int ret = 0;
    const dvp_sensor_config_t **sensors = get_sensor_config_devices_list();
    uint32_t i, size = get_sensor_config_devices_num();


    for (i = 0; i < size; i++)
    {
        char ppi[500] = {0};

        ret = doorbell_get_ppis(ppi, sensors[i]->ppi_cap, sizeof(ppi));

        if (ret >= sizeof(ppi))
        {
            LOGE("doorbell_camera_get_ppis overflow\n");
        }

        os_memset(p, 0, DEVICE_RESPONSE_SIZE);

        LOGV("sensor: %s, ppi: %uX%u\n", sensors[i]->name,
             ppi_to_pixel_x(sensors[i]->def_ppi),
             ppi_to_pixel_y(sensors[i]->def_ppi));
        sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"DVP\", \"ppi\": %s}",
                sensors[i]->name,
                sensors[i]->id,
                ppi);

        LOGD("dump: %s\n", p);

        evt->length = CHECK_ENDIAN_UINT16(strlen(p));
        doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);
    }

#else
    os_memset(p, 0, DEVICE_RESPONSE_SIZE);

    sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"DVP\", \"ppi\":[\"%uX%u\"]}",
            "DVP",
            1,
            ppi_to_pixel_x(0),
            ppi_to_pixel_y(0));
    evt->length = CHECK_ENDIAN_UINT16(strlen(p));
    doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);


#endif
    os_memset(p, 0, DEVICE_RESPONSE_SIZE);

    sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"UVC\", \"ppi\":[\"%uX%u\"]}",
            "UVC",
            UVC_DEVICE_ID,
            ppi_to_pixel_x(0),
            ppi_to_pixel_y(0));
    evt->length = CHECK_ENDIAN_UINT16(strlen(p));
    evt->flags = EVT_FLAGS_COMPLETE;
    doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);

    os_free(evt);

    return 0;
}

int doorbell_devices_set_camera_transfer_callback(void *cb)
{
    avdk_err_t ret = doorbell_video_info_init();
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: doorbell_video_info_init failed\n", __func__);
        return BK_FAIL;
    }

    s_db_video_info->video_transfer_cb = cb;

    return ret;
}