#include <components/bk_camera_ctlr.h>
#include <components/dvp_camera_types.h>
#include "frame/frame_que_v2.h"
#include "doorbell_devices.h"
#include "frame_buffer.h"

#define TAG "db-dvp"

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

static void dvp_camera_frame_complete(image_format_t format, frame_buffer_t *frame, int result)
{
    if (result != AVDK_ERR_OK)
    {
        // 采集失败，通过queue的cancel机制安全释放
        frame_queue_v2_cancel(format, frame);
    }
    else
    {
        // V2版本：采集成功，将帧放入ready队列供消费者使用
        frame_queue_v2_complete(format, frame);
    }
}

static const bk_dvp_callback_t doorbell_dvp_cbs =
{
    .malloc = frame_queue_v2_malloc,
    .complete = dvp_camera_frame_complete,
};

bk_camera_ctlr_handle_t dvp_camera_turn_on(camera_parameters_t *parameters)
{
    avdk_err_t ret = AVDK_ERR_OK;
    bk_camera_ctlr_handle_t handle = NULL;

    if (parameters == NULL)
    {
        LOGE("%s: parameters is NULL\n", __func__);
        return NULL;
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
    }
    else
    {
        dvp_config.img_format = IMAGE_YUV | IMAGE_H264;
    }

    dvp_config.width = parameters->width;
    dvp_config.height = parameters->height;

    bk_dvp_ctlr_config_t dvp_ctlr_config =
    {
        .config = dvp_config,
        .cbs = &doorbell_dvp_cbs,
    };

    ret = bk_camera_dvp_ctlr_new(&handle, &dvp_ctlr_config);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s bk_camera_dvp_ctlr_new failed\n", __func__);
        goto error;
    }

    ret = bk_camera_open(handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s bk_camera_dvp_ctlr_open failed\n", __func__);
        bk_camera_delete(handle);
        goto error;
    }
    else
    {
        LOGD("%s bk_camera_dvp_ctlr_open successful\n", __func__);
    }

    return handle;

error:

    if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
    {
        GPIO_DOWN(DVP_POWER_GPIO_ID);
    }
    return NULL;
}

avdk_err_t dvp_camera_turn_off(bk_camera_ctlr_handle_t handle)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (handle == NULL)
    {
        LOGE("%s: s_db_video_info is NULL\n", __func__);
        return ret;
    }

    ret = bk_camera_close(handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: bk_camera_close failed\n", __func__);
        return ret;
    }

    ret = bk_camera_delete(handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: bk_camera_delete failed\n", __func__);
        return ret;
    }

    // power off dvp
    if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
    {
        GPIO_DOWN(DVP_POWER_GPIO_ID);
    }

    LOGD("%s: bk_camera_delete successful\n", __func__);

    return ret;
}