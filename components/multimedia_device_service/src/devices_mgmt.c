#include <common/bk_include.h>
#include "cli.h"
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <common/bk_err.h>

#include "devices_mgmt.h"

#include "app_camera.h"
#include "app_codec.h"
#include "app_display.h"

#define TAG "db-device"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define UVC_PORT_MAX 3

typedef struct
{
    beken_thread_t thd;
    beken_queue_t queue;
    display_source_t display_source;
    app_uvc_device_t *uvc_device[UVC_PORT_MAX];
} devices_mgmt_info_t;

devices_mgmt_info_t *devices_mgmt_info = NULL;

bk_err_t devices_mgmt_send_msg(uint32_t event, uintptr_t param, uint32_t timeout)
{
    bk_err_t ret = BK_OK;
    devices_mgmt_msg_t msg = {
        .event = event,
        .param = param,
        .result = BK_FAIL,
        .wait = NULL,
    };

    if (devices_mgmt_info == NULL)
    {
        LOGE("%s devices_mgmt_info is null\n", __func__);
        return BK_FAIL;
    }

    if (devices_mgmt_info->queue == NULL)
    {
        LOGE("%s queue is null\n", __func__);
        return BK_FAIL;
    }

    if (timeout != BEKEN_NO_WAIT)
    {
        ret = rtos_init_semaphore(&msg.wait, 1);
        if (BK_OK != ret)
        {
            LOGE("%s init semaphore failed\n", __func__);
            return BK_FAIL;
        }
    }

    ret = rtos_push_to_queue(&devices_mgmt_info->queue, &msg, BEKEN_NO_WAIT);

    if (BK_OK != ret)
    {
        LOGE("%s push message failed\n", __func__);
    }

    if (timeout != BEKEN_NO_WAIT)
    {
        ret = rtos_get_semaphore(&msg.wait, timeout);

        if (BK_OK != ret)
        {
            LOGE("%s get semaphore failed\n", __func__);
        }
        else
        {
            ret = msg.result;
        }
    }

    return ret;
}

static void devices_mgmt_message_handle(void *args)
{
    bk_err_t ret = BK_OK;
    devices_mgmt_msg_t msg;
    devices_mgmt_info_t *info = (devices_mgmt_info_t *)args;

    while (1)
    {
        ret = rtos_pop_from_queue(&info->queue, &msg, BEKEN_WAIT_FOREVER);

        if (BK_OK != ret)
        {
            LOGE("%s pop message failed\n", __func__);
            continue;
        }

        LOGI("###%s, event:%d\n", __func__, msg.event);

        switch (msg.event)
        {
            case DEVICES_MGMT_EXIT:
                goto exit;
                break;

            case DEVICES_MGMT_ISP_TURN_ON:
                break;

            case DEVICES_MGMT_ISP_DVP_TURN_ON:
                break;

            case DEVICES_MGMT_ISP_MIPI_TURN_ON:
                break;

            case DEVICES_MGMT_ISP_DUAL_TURN_ON:
                break;

            case DEVICES_MGMT_ISP_TURN_OFF:
                break;

            case DEVICES_MGMT_MIPI_LCD_TURN_ON:
                break;

            case DEVICES_MGMT_MIPI_LCD_TURN_OFF:
                break;

            case DEVICES_MGMT_ENCODE_ISP_CAMERA_TURN_ON:
                break;

            case DEVICES_MGMT_ENCODE_ISP_CAMERA_TURN_OFF:
                break;

            default:
                break;
        }

        if (msg.wait)
        {
            ret = rtos_set_semaphore(&msg.wait);
            if (BK_OK != ret)
            {
                LOGE("%s set semaphore failed\n", __func__);
            }
        }
    }

exit:

    /* delate msg queue */
    ret = rtos_deinit_queue(&info->queue);

    if (ret != kNoErr)
    {
        LOGE("delate message queue fail\n");
    }

    info->queue = NULL;

    LOGE("delate message queue complete\n");

    /* delate task */
    rtos_delete_thread(NULL);

    info->thd = NULL;

    LOGE("delate task complete\n");
}

int devices_mgmt_init(void)
{
    bk_err_t ret = BK_OK;

    if (devices_mgmt_info != NULL)
    {
        LOGE("%s devices_mgmt_info is already initialized\n", __func__);
        return BK_OK;
    }

    devices_mgmt_info = os_malloc(sizeof(devices_mgmt_info_t));
    if (devices_mgmt_info == NULL)
    {
        LOGE("%s malloc devices_mgmt_info failed\n", __func__);
        return  BK_FAIL;
    }
    os_memset(devices_mgmt_info, 0, sizeof(devices_mgmt_info_t));

    ret = rtos_init_queue(&devices_mgmt_info->queue,
                          "devices_mgmt_queue",
                          sizeof(devices_mgmt_msg_t),
                          DEVICES_MGMT_QUEUE_SIZE);

    if (BK_OK != ret)
    {
        LOGE("%s init queue failed\n", __func__);
        goto error;
    }

    ret = rtos_create_thread(&devices_mgmt_info->thd,
        BEKEN_DEFAULT_WORKER_PRIORITY,
        "devices_mgmt_thread",
        (beken_thread_function_t)devices_mgmt_message_handle,
        1024 * 6,
        devices_mgmt_info);

    if (BK_OK != ret)
    {
        LOGE("%s create thread failed\n", __func__);
        return BK_FAIL;
    }

    devices_cli_init();

    LOGE("%s init devices_mgmt success\n", __func__);

    return BK_OK;

error:

    if (devices_mgmt_info != NULL)
    {
        if (devices_mgmt_info->queue != NULL)
        {
            rtos_deinit_queue(&devices_mgmt_info->queue);
        }

        if (devices_mgmt_info->thd != NULL)
        {
            rtos_delete_thread(&devices_mgmt_info->thd);
        }

        os_free(devices_mgmt_info);
        devices_mgmt_info = NULL;
    }

    LOGI("%s init devices_mgmt failed\n", __func__);

    return ret;
}

void devices_mgmt_deinit(void)
{
    //TODO FIXME

    if (devices_mgmt_info)
    {
        os_free(devices_mgmt_info);
        devices_mgmt_info = NULL;
    }
}


bk_err_t devices_mgmt_set_display_source(display_stream_id_t id, void *args)
{
    bk_err_t ret = BK_OK;
    devices_mgmt_info->display_source.id = id;
    devices_mgmt_info->display_source.args = args;
    return ret;
}

bk_err_t devices_mgmt_add_uvc_device(app_uvc_device_t *device, uint8_t port)
{
    //bk_err_t ret = BK_OK;
    if (devices_mgmt_info->uvc_device[port] == NULL)
    {
        devices_mgmt_info->uvc_device[port] = (app_uvc_device_t *)os_malloc(sizeof(app_uvc_device_t));
        if (devices_mgmt_info->uvc_device[port] == NULL)
        {
            LOGE("malloc uvc device failed\n");
            return BK_FAIL;
        }
    }

    os_memcpy(devices_mgmt_info->uvc_device[port], device, sizeof(app_uvc_device_t));

    return BK_OK;
}

app_uvc_device_t *devices_mgmt_get_uvc_device(uint8_t port)
{
    return devices_mgmt_info->uvc_device[port];
}

display_source_t *devices_mgmt_get_display_source(void)
{
    return &devices_mgmt_info->display_source;
}