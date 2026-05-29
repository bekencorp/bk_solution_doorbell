#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include "doorbell_comm.h"

#include "doorbell_cmd.h"
#include "doorbell_devices.h"
#include "modules/wifi.h"
#include "app_camera.h"
#include "app_display.h"
#include "app_codec.h"
#include "app_gpu.h"
#include "app_jpeg_decode.h"
#include <components/bk_uvc_camera_types.h>
#include "mds_img_manager.h"
#include <lcd/lcd_mipi_hx8399c_1080x1920.h>
#include "devices_mgmt.h"
#include <sys_types.h>
#include <modules/pm.h>
#if CONFIG_INTEGRATION_DOORBELL_KVS
#include "doorbell_kvs_network_transfer.h"
#endif
#define TAG "db-device"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

typedef enum
{
    LCD_STATUS_CLOSE,
    LCD_STATUS_OPEN,
    LCD_STATUS_UNKNOWN,
} lcd_status_t;

db_device_info_t *db_device_info = NULL;

typedef struct
{
    uint8_t enable;
    uint8_t port_id;
    bk_image_format_t img_format;
    beken_semaphore_t sem;
    beken_thread_t transfer_thread;
} db_trans_cfg_t;
static db_trans_cfg_t *s_db_trans_cfg = NULL;

int doorbell_get_supported_camera_devices(int opcode)
{
    db_evt_head_t *evt = psram_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
    char *p = (char *)(evt + 1);

    evt->opcode = opcode;
    evt->status = EVT_STATUS_OK;
    evt->flags = EVT_FLAGS_CONTINUE;

    LOGD("DBCMD_GET_CAMERA_SUPPORTED_DEVICES\n");

    os_memset(p, 0, DEVICE_RESPONSE_SIZE);

    sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"UVC\", \"ppi\":[\"%dX%d\"]}",
            "UVC", UVC_DEVICE_ID, 1920, 1080);
    evt->length = CHECK_ENDIAN_UINT16(strlen(p));
    evt->flags = EVT_FLAGS_COMPLETE;

    ntwk_trans_ctrl_send((uint8_t *)evt, sizeof(db_evt_head_t) + evt->length);

    os_free(evt);

    return 0;
}

int doorbell_get_supported_lcd_devices(int opcode)
{
    db_evt_head_t *evt = psram_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
    char *p = (char *)(evt + 1);

    evt->opcode = opcode;
    evt->status = EVT_STATUS_OK;
    evt->flags = EVT_FLAGS_CONTINUE;

    LOGD("DBCMD_GET_LCD_SUPPORTED_DEVICES\n");

    // sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"%s\", \"ppi\":\"%dX%d\"}",
    //     "aml01", LCD_PANEL_AML01, "rgb", 720, 1280);

    evt->length = CHECK_ENDIAN_UINT16(strlen(p));

    evt->flags = EVT_FLAGS_COMPLETE;
    ntwk_trans_ctrl_send((uint8_t *)evt, sizeof(db_evt_head_t) + evt->length);

    os_free(evt);

    return 0;
}

int doorbell_get_lcd_status(int opcode)
{
    uint32_t lcd_status = db_device_info->lcd_enable ? LCD_STATUS_OPEN : LCD_STATUS_CLOSE;

    db_evt_head_t *evt = psram_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
    char *p = (char *)(evt + 1);

    evt->opcode = opcode;
    evt->status = EVT_STATUS_OK;
    evt->flags = EVT_FLAGS_CONTINUE;

    LOGD("DBCMD_GET_LCD_STATUS\n");
    os_memset(p, 0, DEVICE_RESPONSE_SIZE);

    if (lcd_status != LCD_STATUS_CLOSE && lcd_status != LCD_STATUS_OPEN)
    {
        lcd_status = LCD_STATUS_UNKNOWN;
    }
    sprintf(p, "{\"status\": \"%u\"}", lcd_status);
    LOGD("dump: %s\n", p);
    evt->length = CHECK_ENDIAN_UINT16(strlen(p));

    evt->flags = EVT_FLAGS_COMPLETE;

    ntwk_trans_ctrl_send((uint8_t *)evt, sizeof(db_evt_head_t) + evt->length);

    os_free(evt);

    return 0;
}

static int gpu_pipeline_attach(db_device_info_t *info)
{
    int ret;

    if (info == NULL)
    {
        return BK_FAIL;
    }
    if (info->gpu_handle != NULL)
    {
        return BK_OK;
    }

    display_source_t *src = devices_mgmt_get_display_source();
    if (src == NULL)
    {
        LOGE("%s, display_source not found\n", __func__);
        return BK_FAIL;
    }

    if (src->id == DISPLAY_STREAM_ID_MIPI_CSI)
    {
        if (info->isp_handle == NULL)
        {
            LOGE("%s, isp_handle is NULL\n", __func__);
            return BK_FAIL;
        }

        ret = app_gpu_turn_on(app_gpu_board_config_get());
        if (ret != BK_OK)
        {
            LOGE("%s, app_gpu_turn_on failed, ret = %d\n", __func__, ret);
            return ret;
        }

        info->gpu_handle = app_gpu_handle_get();
        if (info->gpu_handle == NULL)
        {
            LOGE("%s, app_gpu_handle_get failed\n", __func__);
            return BK_FAIL;
        }

        ret = bk_flexa_isp_gpu_bond_start(&info->gpu_bond, info->isp_handle, info->gpu_handle);
        if (ret != BK_OK)
        {
            LOGE("%s, bk_flexa_isp_gpu_bond_start failed, ret = %d\n", __func__, ret);
            (void)app_gpu_turn_off(info->gpu_handle);
            info->gpu_handle = NULL;
            return ret;
        }
    }
    else
    {
#ifdef CONFIG_USB_CAMERA
        if (info->decode_handle == NULL)
        {
            LOGE("%s, decode_handle is NULL\n", __func__);
            return BK_FAIL;
        }

        app_uvc_device_t *uvc_device = devices_mgmt_get_uvc_device(0);
        if (uvc_device == NULL)
        {
            LOGE("%s, uvc_device not found\n", __func__);
            return BK_FAIL;
        }

        display_board_config_t *display_board = app_display_board_config_get();
        if (display_board != NULL && display_board->mipi.panel != NULL)
        {
            gpu_board_config_t *gpu_board = app_gpu_board_config_get();
            if (gpu_board != NULL &&
                (uvc_device->width != display_board->mipi.panel->timing.h_size ||
                 uvc_device->height != display_board->mipi.panel->timing.v_size))
            {
                gpu_board->flexa.scale = true;
            }
        }

        ret = app_gpu_v2_turn_on(uvc_device->width, uvc_device->height);
        if (ret != BK_OK)
        {
            LOGE("%s, app_gpu_v2_turn_on failed, ret = %d\n", __func__, ret);
            return ret;
        }

        info->gpu_handle = app_gpu_handle_get();
        if (info->gpu_handle == NULL)
        {
            LOGE("%s, app_gpu_handle_get failed\n", __func__);
            return BK_FAIL;
        }

        ret = bk_flexa_mjpegd_gpu_bond_start(&info->gpu_bond, info->decode_handle, info->gpu_handle);
        if (ret != BK_OK)
        {
            LOGE("%s, bk_flexa_mjpegd_gpu_bond_start failed, ret = %d\n", __func__, ret);
            (void)app_gpu_turn_off(info->gpu_handle);
            info->gpu_handle = NULL;
            return ret;
        }
#else
        LOGE("%s, UVC source but CONFIG_USB_CAMERA not enabled\n", __func__);
        return BK_FAIL;
#endif
    }

    return BK_OK;
}

static int gpu_pipeline_detach(db_device_info_t *info)
{
    if (info == NULL)
    {
        LOGE("%s, info is NULL\n", __func__);
        return BK_FAIL;
    }

    if (info->gpu_bond != NULL)
    {
        display_source_t *src = devices_mgmt_get_display_source();
        if (src != NULL && src->id == DISPLAY_STREAM_ID_MIPI_CSI)
        {
            bk_flexa_isp_gpu_bond_stop(info->gpu_bond);
        }
        else
        {
#ifdef CONFIG_USB_CAMERA
            bk_flexa_mjpegd_gpu_bond_stop(info->gpu_bond);
#endif
        }
        info->gpu_bond = NULL;
    }

    if (info->gpu_handle != NULL)
    {
        avdk_err_t off_ret = app_gpu_turn_off(info->gpu_handle);
        if (off_ret != BK_OK)
        {
            LOGE("%s, app_gpu_turn_off failed, ret = %d\n", __func__, off_ret);
        }
        info->gpu_handle = NULL;
    }

    return BK_OK;
}

int doorbell_camera_turn_on(camera_parameters_t *parameters)
{
    bk_err_t ret = BK_FAIL;
    LOGD("%s, id: %d, %d X %d, format: %d, Protocol: %d\n", __func__,
         parameters->id, parameters->width, parameters->height,
         parameters->format, parameters->protocol);

    db_device_info_t *info = db_device_info;
    if (info == NULL)
    {
        LOGE("%s, info not init  %d\n", __func__, __LINE__);
        return ret;
    }

    if (info->video_enable)
    {
        LOGE("%s, already open %d\n", __func__, __LINE__);
        ret = BK_OK;
        return ret;
    }

    info->camera_id = parameters->id;
    if (parameters->format)
    {
        info->transfer_format = BK_IMAGE_FORMAT_H264;
    }
    else
    {
        info->transfer_format = BK_IMAGE_FORMAT_MJPEG;
    }
    if (parameters->id == UVC_DEVICE_ID)
    {
#ifdef CONFIG_USB_CAMERA
        camera_parameters_ext_t ext_parameters = {
            .camera_width = parameters->width,
            .camera_height = parameters->height,
            .camera_out_format = parameters->format,
            .port = 1,
        };
        ret = app_uvc_turn_on(&ext_parameters);
        if (ret != BK_OK)
        {
            LOGE("%s, app_uvc_turn_on failed, ret = %d\n", __func__, ret);
            goto err;
        }

        ret = app_jpeg_decode_open(parameters->width, parameters->height, BK_IMAGE_FORMAT_MJPEG, 1);
        if (ret != BK_OK)
        {
            LOGE("%s, decode_test_open failed, ret = %d\n", __func__, ret);
            goto err;
        }
        ret = app_jpeg_decode_get_handle(&info->decode_handle);
        if (ret != BK_OK)
        {
            LOGE("%s, app_jpeg_decode_get_handle failed, ret = %d\n", __func__, ret);
            goto err;
        }
        ret = app_h264_encode_open(parameters->width, parameters->height);
        if (ret != BK_OK)
        {
            LOGE("%s, h264_encode_open failed, ret = %d\n", __func__, ret);
            goto err;
        }

        ret = app_h264_encode_get_handle(&info->encode_handle);
        if (ret != BK_OK)
        {
            LOGE("%s, app_h264_encode_get_handle failed, ret = %d\n", __func__, ret);
            goto err;
        }

        LOGI("%s %d decode_handle = %p, encode_handle = %p\r\n", __func__, __LINE__, info->decode_handle, info->encode_handle);

        ret = bk_flexa_mjpegd_h264e_bond_start(&info->h264e_bond, info->decode_handle, info->encode_handle);
        if (ret != BK_OK)
        {
            LOGE("%s, bk_flexa_mjpegd_h264e_bond_start failed, ret = %d\n", __func__, ret);
            goto err;
        }

        if (info->lcd_enable) {
            ret = gpu_pipeline_attach(info);
            if (ret != BK_OK) {
                LOGE("%s, gpu_pipeline_attach failed, ret = %d\n", __func__, ret);
                goto err;
            }
        }
#endif
    }
    else
    {
        ret = app_isp_mipi_camera_turn_on(app_camera_board_config_get());

        if (ret != BK_OK)
        {
            LOGE("app_isp_mipi_camera_turn_on failed\n");
            goto err;
        }

        ret = devices_mgmt_set_display_source(DISPLAY_STREAM_ID_MIPI_CSI, NULL);

        if (ret != BK_OK)
        {
            LOGE("devices_mgmt_set_display_source failed\n");
            goto err;
        }

        ret = app_h264e_turn_on();

        if (ret != BK_OK)
        {
            LOGE("app_h264e_turn_on failed\n");
            goto err;
        }

        info->isp_handle = app_isp_handle_get();
        if (info->isp_handle == NULL) {
            LOGE("%s, app_isp_handle_get failed\n", __func__);
            goto err;
        }

        info->encode_handle = app_h264_encode_handle_get();
        if (info->encode_handle == NULL) {
            LOGE("%s, app_h264_encode_handle_get failed\n", __func__);
            goto err;
        }

        ret = bk_flexa_isp_h264e_bond_start(&info->h264e_bond, info->isp_handle, info->encode_handle);
        if (ret != BK_OK) {
            LOGE("%s, bk_flexa_isp_bond_start failed, ret = %d\n", __func__, ret);
            goto err;
        }

        if (info->lcd_enable) {
            ret = gpu_pipeline_attach(info);
            if (ret != BK_OK) {
                LOGE("%s, gpu_pipeline_attach failed, ret = %d\n", __func__, ret);
                goto err;
            }
        }

    }

    if (ret != BK_OK)
    {
        LOGE("%s, camera turn on failed, ret = %d\n", __func__, ret);
        goto err;
    }

    info->video_enable = true;

    LOGD("%s success\n", __func__);

    return BK_OK;

err:
    return ret;
}

int doorbell_camera_turn_off(void)
{
    int ret = BK_OK;

    db_device_info_t *info = db_device_info;
    if (info == NULL || info->video_enable == false)
    {
        LOGE("%s, already close %d\n", __func__, __LINE__);
        return ret;
    }

    if (info->camera_id == UVC_DEVICE_ID)
    {
#ifdef CONFIG_USB_CAMERA
        gpu_pipeline_detach(info);

        if (info->h264e_bond != NULL) {
            bk_flexa_mjpegd_h264e_bond_stop(info->h264e_bond);
            info->h264e_bond = NULL;
        }

        ret = app_uvc_turn_off(1);//default port id is 1
        if (ret != BK_OK)
        {
            LOGE("%s, app_uvc_turn_off failed, ret = %d\n", __func__, ret);
            return ret;
        }
        ret = app_h264_encode_close();
        if (ret != BK_OK)
        {
            LOGE("%s, h264_encode_close failed, ret = %d\n", __func__, ret);
            return ret;
        }
        ret = app_jpeg_decode_close();
        if (ret != BK_OK)
        {
            LOGE("%s, jpeg_decode_close failed, ret = %d\n", __func__, ret);
            return ret;
        }
        info->decode_handle = NULL;
        info->encode_handle = NULL;
#endif
    }
    else
    {
        gpu_pipeline_detach(info);

        bk_flexa_isp_h264e_bond_stop(info->h264e_bond);

        info->isp_handle = NULL;
        info->encode_handle = NULL;
        info->h264e_bond = NULL;

        ret = app_h264e_turn_off();
        if (ret != BK_OK)
        {
            LOGE("%s, app_h264e_turn_off failed, ret = %d\n", __func__, ret);
            ret = BK_FAIL;
            return ret;
        }

        ret = app_isp_camera_turn_off();

    }

    if (ret != BK_OK)
    {
        LOGE("%s, camera turn off failed, ret = %d\n", __func__, ret);
        ret = BK_FAIL;
        return ret;
    }

    info->video_enable = false;
    LOGD("%s success\n", __func__);

    return ret;
}

int doorbell_video_transfer_turn_on(void)
{
    int ret = BK_FAIL;

    db_device_info_t *info = db_device_info;
    if (info == NULL)
    {
        LOGE("%s, info not init  %d\n", __func__, __LINE__);
        return ret;
    }

    if (info->video_enable == false)
    {
        LOGE("%s: video not open!\n", __func__);
        return ret;
    }

    ret = doorbell_devices_start(info->transfer_format);
    if (ret == BK_OK)
    {
        LOGD("%s, success\n", __func__);
    }

    return ret;
}

int doorbell_video_transfer_turn_off(void)
{
    int ret = BK_OK;
    db_device_info_t *info = db_device_info;
    if (info == NULL)
    {
        LOGE("%s, info not init  %d\n", __func__, __LINE__);
        return ret;
    }

    if (info->video_enable == false)
    {
        LOGE("%s: video not open!\n", __func__);
        return ret;
    }

    ret = doorbell_devices_stop();
    if (ret == BK_OK)
    {
        LOGD("%s, success\n", __func__);
    }

#if (CONFIG_INTEGRATION_DOORBELL_CS2)
   // ntwk_trans_cs2_video_timer_deinit();
#endif

    return ret;
}

int doorbell_display_turn_on(display_board_config_t *config)
{
    int ret = BK_FAIL;

    db_device_info_t *info = db_device_info;

    //LOGD("%s, id: %d, rotate: %d fmt: %d\n", __func__, parameters->id, parameters->rotate_angle, parameters->pixel_format);

    if (info->lcd_enable == true)
    {
        //LOGD("%s, id: %d already open\n", __func__, parameters->id);
        return ret;
    }

    display_source_t *display_source = devices_mgmt_get_display_source();

    if (display_source == NULL)
    {
        LOGE("%s, display_source not found\n", __func__);
        return ret;
    }

    if (display_source->id == DISPLAY_STREAM_ID_INVALID)
    {
        LOGE("%s, display_source id is invalid\n", __func__);
        return ret;
    }
    ret = app_mipi_lcd_turn_on(config);
    if (ret != BK_OK)
    {
        LOGE("%s, app_mipi_lcd_turn_on failed, ret = %d\n", __func__, ret);
        return ret;
    }

    if (info->video_enable) {
        ret = gpu_pipeline_attach(info);
        if (ret != BK_OK) {
            LOGE("%s, gpu_pipeline_attach failed, ret = %d\n", __func__, ret);
            goto error;
        }
    }

    info->lcd_enable = true;
    LOGD("%s success\n", __func__);
    return BK_OK;

error:
    gpu_pipeline_detach(info);
    ret = app_mipi_lcd_turn_off();
    if (ret != BK_OK)
    {
        LOGE("%s, app_mipi_lcd_turn_off failed, ret = %d\n", __func__, ret);
    }
    info->lcd_enable = false;
    LOGD("%s failed\n", __func__);
    return BK_FAIL;
}

int doorbell_display_turn_off(void)
{
    int ret = BK_OK;
    db_device_info_t *info = db_device_info;

    if (info->lcd_enable == false)
    {
        LOGD("%s, %d already close\n", __func__);
        return EVT_STATUS_ALREADY;
    }

    gpu_pipeline_detach(info);

    ret = app_mipi_lcd_turn_off();
    if (ret != BK_OK)
    {
        LOGE("%s, app_mipi_lcd_turn_off failed, ret = %d\n", __func__, ret);
        return ret;
    }

    info->lcd_enable = false;
    LOGD("%s success\n", __func__);

    return ret;
}

int doorbell_devices_init(void)
{
    if (db_device_info == NULL)
    {
        db_device_info = os_malloc(sizeof(db_device_info_t));
    }

    if (db_device_info == NULL)
    {
        LOGE("malloc db_device_info failed");
        return  BK_FAIL;
    }

    os_memset(db_device_info, 0, sizeof(db_device_info_t));

    return BK_OK;
}

void doorbell_devices_deinit(void)
{
    if (db_device_info)
    {
        if (db_device_info->video_enable == true)
        {
            LOGW("%s, video not close, please turn off manually\n", __func__);
            return;
        }

        if (db_device_info->lcd_enable == true)
        {
            LOGW("%s, display not close, please turn off manually\n", __func__);
            return;
        }

        os_free(db_device_info);
        db_device_info = NULL;
    }
}

bk_err_t doorbell_devices_stop(void)
{
    if (s_db_trans_cfg == NULL)
    {
        return BK_OK;
    }

    if (!s_db_trans_cfg->enable)
    {
        LOGE("%s, have been close!\r\n", __func__);
        return BK_FAIL;
    }

    s_db_trans_cfg->enable = 0;
    rtos_get_semaphore(&s_db_trans_cfg->sem, BEKEN_NEVER_TIMEOUT);

    bk_wifi_set_wifi_media_mode(false);

    bk_wifi_set_video_quality(WIFI_VIDEO_QUALITY_HD);
    if (s_db_trans_cfg->transfer_thread)
    {
        rtos_delete_thread(s_db_trans_cfg->transfer_thread);
        s_db_trans_cfg->transfer_thread = NULL;
    }
    if (s_db_trans_cfg->sem)
    {
        rtos_deinit_semaphore(&s_db_trans_cfg->sem);
        s_db_trans_cfg->sem = NULL;
    }
    os_free(s_db_trans_cfg);
    s_db_trans_cfg = NULL;

    LOGD("%s, close success!\r\n", __func__);

    return BK_OK;
}

bk_err_t doorbell_devices_set_transfer_port(uint8_t port_id)
{
    // only used for uvc
    if (s_db_trans_cfg == NULL)
    {
        return BK_FAIL;
    }
    if (port_id > UVC_PORT_MAX)
    {
        LOGE("%s, port_id out of range\n", __func__);
        return BK_FAIL;
    }
    s_db_trans_cfg->port_id = port_id;
    return BK_OK;
}

static void doorbell_devices_task_entry(beken_thread_arg_t data)
{
    db_trans_cfg_t *cfg = (db_trans_cfg_t *)data;
    frame_buffer_t *frame = NULL;
    cfg->enable = true;
    rtos_set_semaphore(&cfg->sem);
    uint8_t log_enable = 0;
    bk_err_t ret = BK_OK;

    while (cfg->enable)
    {
        frame = bk_encoded_complete_data_request(50);

        if (frame == NULL)
        {
            log_enable ++;
            if (log_enable > 100)
            {
                LOGD("%s, read frame null format:%x\n", __func__, cfg->img_format);
                log_enable = 0;
            }
            continue;
        }

        if (frame->sequence < 5)
        {
            #if CONFIG_INTEGRATION_DOORBELL_KVS
            char buf[64];
            doorbell_kvs_get_format_utc_ts(buf, sizeof(buf));
            LOGD("%s, frame sequence %d, utc: %s\n", __func__, frame->sequence, buf);
            #else
            LOGD("%s, frame sequence %d\n", __func__, frame->sequence);
            #endif
        }

        log_enable = 0;

        if (cfg->port_id == 0)
        {
            ret = ntwk_trans_video_send((uint8_t *)frame, frame->length, cfg->img_format);
            if (ret != BK_OK)
            {
                LOGV("%s,failed, ret = %d\n", __func__, ret);
            }
        }
        else
        {
            // if (frame->h264_type == cfg->port_id)
            {
                ret = ntwk_trans_video_send((uint8_t *)frame, frame->length, BK_IMAGE_FORMAT_H264);
                if (ret != BK_OK)
                {
                    LOGV("%s,failed1, ret = %d\n", __func__, ret);
                }
            }
        }

        bk_encoded_data_free_request((uint8_t *)frame);
        frame = NULL;
    }

    cfg->transfer_thread = NULL;
    rtos_set_semaphore(&cfg->sem);
    rtos_delete_thread(NULL);
}

bk_err_t doorbell_devices_start(uint16_t img_format)
{
    if (s_db_trans_cfg)
    {
        LOGW("%s, already opened, img_format: %d", __func__, s_db_trans_cfg->img_format);
        return BK_OK;
    }

    s_db_trans_cfg = os_malloc(sizeof(db_trans_cfg_t));
    if (s_db_trans_cfg == NULL)
    {
        LOGE("%s malloc failed\r\n", __func__);
        return BK_ERR_NO_MEM;
    }

    memset(s_db_trans_cfg, 0, sizeof(db_trans_cfg_t));

    bk_encoded_data_manager_init();

    s_db_trans_cfg->img_format = img_format;

    if (rtos_init_semaphore(&s_db_trans_cfg->sem, 1) != BK_OK)
    {
        LOGE("%s rtos_init_semaphore failed\n", __func__);
        goto error;
    }

    // need create task to read frame
    bk_err_t ret = rtos_create_hsram_thread(&s_db_trans_cfg->transfer_thread,
                                BEKEN_DEFAULT_WORKER_PRIORITY,
                                "trs_task",
                                (beken_thread_function_t)doorbell_devices_task_entry,
                                4096,
                                (beken_thread_arg_t)s_db_trans_cfg);

    if (BK_OK != ret)
    {
        LOGE("%s transfer_app_task init failed\n", __func__);
        ret = BK_ERR_NO_MEM;
        goto error;
    }

    rtos_get_semaphore(&s_db_trans_cfg->sem, BEKEN_NEVER_TIMEOUT);

    bk_wifi_set_wifi_media_mode(true);

    bk_wifi_set_video_quality(WIFI_VIDEO_QUALITY_SD);

    return BK_OK;

error:
    doorbell_devices_stop();
    return BK_FAIL;
}


