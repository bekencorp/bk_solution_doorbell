#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include "doorbell_comm.h"

#include "doorbell_cmd.h"
#include "doorbell_devices.h"
#include "doorbell_devices_intercom.h"
#include "doorbell_downlink_video.h"
#include "network_transfer.h"
#include "doorbell_selftest.h"
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
#if CONFIG_MDS_SNAPSHOT || CONFIG_H264_QP_PRESET_QUALITY || CONFIG_H264_QP_PRESET_FIXED_QP || CONFIG_H264_QP_PRESET_BALANCED || CONFIG_H264_QP_PRESET_ANTI_STUTTER || CONFIG_H264_QP_PRESET_LAN_HD
#include <components/bk_encode/bk_h264_encode_ctlr.h>
#endif
#ifdef CONFIG_MDS_SNAPSHOT
#include "bk_snapshot.h"
#include "bk_snapshot_sw.h"
#endif
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

void *doorbell_devices_isp_handle_get(void)
{
    return (db_device_info != NULL) ? db_device_info->isp_handle : NULL;
}

bool doorbell_devices_uplink_active(void)
{
    return (db_device_info != NULL && db_device_info->video_enable != 0U);
}

#if CONFIG_H264_QP_PRESET_QUALITY || CONFIG_H264_QP_PRESET_FIXED_QP || CONFIG_H264_QP_PRESET_BALANCED || CONFIG_H264_QP_PRESET_ANTI_STUTTER || CONFIG_H264_QP_PRESET_LAN_HD
static bk_err_t doorbell_apply_h264_qp_preset(void *encode_handle)
{
    bk_h264_encode_rate_ctrl_t rate_ctrl = {0};
    const char *preset_name = NULL;
    avdk_err_t ret;

    if (encode_handle == NULL)
    {
        return BK_ERR_PARAM;
    }

#if CONFIG_H264_QP_PRESET_QUALITY
    preset_name = "quality";
    rate_ctrl.bitrate = 2000000;
    rate_ctrl.qp_min_i = 20;
    rate_ctrl.qp_max_i = 40;
    rate_ctrl.qp_min_p = 24;
    rate_ctrl.qp_max_p = 40;
#elif CONFIG_H264_QP_PRESET_FIXED_QP
    preset_name = "fixed-qp";
    rate_ctrl.bitrate = 0;
    rate_ctrl.qp_min_i = 21;
    rate_ctrl.qp_max_i = 21;
    rate_ctrl.qp_min_p = 26;
    rate_ctrl.qp_max_p = 26;
#elif CONFIG_H264_QP_PRESET_BALANCED
    preset_name = "balanced";
    rate_ctrl.bitrate = 1500000;
    rate_ctrl.qp_min_i = 20;
    rate_ctrl.qp_max_i = 51;
    rate_ctrl.qp_min_p = 26;
    rate_ctrl.qp_max_p = 51;
#elif CONFIG_H264_QP_PRESET_ANTI_STUTTER
    preset_name = "anti-stutter";
    rate_ctrl.bitrate = 1200000;
    rate_ctrl.qp_min_i = 24;
    rate_ctrl.qp_max_i = 45;
    rate_ctrl.qp_min_p = 28;
    rate_ctrl.qp_max_p = 48;
#elif CONFIG_H264_QP_PRESET_LAN_HD
    preset_name = "lan-hd";
    rate_ctrl.bitrate = 3000000;
    rate_ctrl.qp_min_i = 18;
    rate_ctrl.qp_max_i = 36;
    rate_ctrl.qp_min_p = 22;
    rate_ctrl.qp_max_p = 38;
#endif

    ret = bk_h264_encode_set_rate_ctrl((bk_h264_encode_ctlr_handle_t)encode_handle, &rate_ctrl);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, apply h264 qp preset failed, ret = %d\n", __func__, ret);
        return BK_FAIL;
    }

    LOGI("h264 qp preset %s: bitrate=%u i=[%u,%u] p=[%u,%u]\n",
         preset_name, rate_ctrl.bitrate, rate_ctrl.qp_min_i, rate_ctrl.qp_max_i,
         rate_ctrl.qp_min_p, rate_ctrl.qp_max_p);

    return BK_OK;
}
#endif

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

    /* Intercom GPU-ownership guard: while the downlink pipeline runs, the
     * downlink compositor already owns the single GPU + HSRAM FLEXA ring and
     * renders the PIP self-view itself. Running the single-view preview GPU path
     * here would call app_gpu_turn_on -> bk_get_gpu_flexa_buffer on an
     * already-owned buffer and fail, aborting camera.turnOn, rolling back the
     * ISP/encoder and freezing the compositor display. The uplink ISP->H264
     * encode bond is independent of the GPU, so skipping the attach lets uplink
     * encode + downlink display coexist. */
    if (doorbell_downlink_video_is_running())
    {
        LOGI("gpu_pipeline_attach: downlink compositor owns GPU, skip preview attach\n");
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

int doorbell_devices_preview_gpu_detach(void)
{
    db_device_info_t *info = db_device_info;

    if (info == NULL)
    {
        return BK_OK;
    }
    if (info->gpu_handle == NULL && info->gpu_bond == NULL)
    {
        return BK_OK;
    }
    LOGI("%s, releasing single-view preview GPU for downlink compositor\n", __func__);
    return gpu_pipeline_detach(info);
}

int doorbell_devices_preview_gpu_attach(void)
{
    db_device_info_t *info = db_device_info;

    if (info == NULL)
    {
        return BK_OK;
    }
    /* Only the single-view preview needs the GPU; requires both capture + LCD. */
    if (!info->video_enable || !info->lcd_enable)
    {
        return BK_OK;
    }
    if (info->gpu_handle != NULL)
    {
        return BK_OK;
    }
    LOGI("%s, restoring single-view preview GPU\n", __func__);
    return gpu_pipeline_attach(info);
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

#if CONFIG_H264_QP_PRESET_QUALITY || CONFIG_H264_QP_PRESET_FIXED_QP || CONFIG_H264_QP_PRESET_BALANCED || CONFIG_H264_QP_PRESET_ANTI_STUTTER || CONFIG_H264_QP_PRESET_LAN_HD
        ret = doorbell_apply_h264_qp_preset(info->encode_handle);
        if (ret != BK_OK)
        {
            goto err;
        }
#endif

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

#if CONFIG_H264_QP_PRESET_QUALITY || CONFIG_H264_QP_PRESET_FIXED_QP || CONFIG_H264_QP_PRESET_BALANCED || CONFIG_H264_QP_PRESET_ANTI_STUTTER || CONFIG_H264_QP_PRESET_LAN_HD
        ret = doorbell_apply_h264_qp_preset(info->encode_handle);
        if (ret != BK_OK)
        {
            goto err;
        }
#endif

        ret = bk_flexa_isp_h264e_bond_start(&info->h264e_bond, info->isp_handle, info->encode_handle);
        if (ret != BK_OK) {
            LOGE("%s, bk_flexa_isp_bond_start failed, ret = %d\n", __func__, ret);
            goto err;
        }

#ifdef CONFIG_MDS_SNAPSHOT
        /* Pre-allocate snapshot buffers only; open SP on capture to avoid idle ISP load. */
        (void)bk_snapshot_sw_prepare();
#endif

        if (info->lcd_enable) {
            ret = gpu_pipeline_attach(info);
            if (ret != BK_OK) {
                LOGE("%s, gpu_pipeline_attach failed, ret = %d\n", __func__, ret);
                goto err;
            }
            /* If the downlink compositor already owns the GPU (downlink started
             * before the camera), gpu_pipeline_attach above is a no-op; render the
             * local self-view by enabling the compositor PIP now that the ISP SP
             * source exists. */
            if (doorbell_downlink_video_is_running()) {
                LOGI("camera on while downlink running: enabling compositor PIP\n");
                (void)doorbell_downlink_pip_enable();
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
    gpu_pipeline_detach(info);

    if (info->camera_id == UVC_DEVICE_ID)
    {
#ifdef CONFIG_USB_CAMERA
        if (info->h264e_bond != NULL)
        {
            bk_flexa_mjpegd_h264e_bond_stop(info->h264e_bond);
            info->h264e_bond = NULL;
        }
        app_uvc_turn_off(1);
        app_h264_encode_close();
        app_jpeg_decode_close();
        info->decode_handle = NULL;
        info->encode_handle = NULL;
#endif
    }
    else
    {
        if (info->h264e_bond != NULL)
        {
            bk_flexa_isp_h264e_bond_stop(info->h264e_bond);
            info->h264e_bond = NULL;
        }
        info->isp_handle = NULL;
        info->encode_handle = NULL;
        app_h264e_turn_off();
#ifdef CONFIG_MDS_SNAPSHOT
        (void)bk_snapshot_sw_deinit();
#endif
        app_isp_camera_turn_off();
    }

    info->video_enable = false;
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

    /* Turning the local uplink/camera off: first drop the downlink PIP self-view
     * so the compositor stops blitting the ISP SP frames (small window would
     * otherwise freeze on the last SP frame). Must run before the ISP teardown
     * below so the PIP task is joined while its SP source is still valid. */
    if (doorbell_downlink_video_is_running())
    {
        (void)doorbell_downlink_pip_disable();
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

#ifdef CONFIG_MDS_SNAPSHOT
        (void)bk_snapshot_sw_deinit();
#endif
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

   // ntwk_trans_cs2_video_timer_deinit();

    return ret;
}

bk_err_t doorbell_devices_force_idr(void)
{
    if (db_device_info == NULL || db_device_info->encode_handle == NULL)
    {
        return BK_FAIL;
    }
    (void)bk_h264_encode_force_idr(
        (bk_h264_encode_ctlr_handle_t)db_device_info->encode_handle);
    return BK_OK;
}

#ifdef CONFIG_MDS_SNAPSHOT
bk_err_t doorbell_isp_snapshot_sw_capture(bk_snapshot_image_t *out_image)
{
    bk_snapshot_sw_config_t cfg = {0};
    avdk_err_t ret;

    if (out_image == NULL)
    {
        return BK_ERR_PARAM;
    }

    os_memset(out_image, 0, sizeof(*out_image));

    if (db_device_info == NULL || db_device_info->video_enable == false)
    {
        LOGE("%s, video pipeline not running\n", __func__);
        return BK_FAIL;
    }

    if (db_device_info->camera_id == UVC_DEVICE_ID)
    {
        LOGE("%s, UVC path not supported\n", __func__);
        return BK_ERR_NOT_SUPPORT;
    }

    cfg.camera_handle = app_isp_camera_ctlr_handle_get();
    if (cfg.camera_handle == NULL)
    {
        LOGE("%s, camera handle is null\n", __func__);
        return BK_FAIL;
    }

    cfg.width = BK_SNAPSHOT_SW_DEFAULT_WIDTH;
    cfg.height = BK_SNAPSHOT_SW_DEFAULT_HEIGHT;
    cfg.jpeg_quality = BK_SNAPSHOT_SW_DEFAULT_QUALITY;
    cfg.read_timeout_ms = BK_SNAPSHOT_SW_DEFAULT_TIMEOUT_MS;

    ret = bk_snapshot_sw_capture(&cfg, out_image);
    if (ret != AVDK_ERR_OK || out_image->size == 0)
    {
        LOGE("%s, bk_snapshot_sw_capture failed ret=%d size=%u\n", __func__, ret, out_image->size);
        return BK_FAIL;
    }

    if (db_device_info->encode_handle != NULL) {
        (void)bk_h264_encode_force_idr((bk_h264_encode_ctlr_handle_t)db_device_info->encode_handle);
    }

    LOGI("%s, ok size=%u %ux%u\n", __func__, out_image->size, out_image->width, out_image->height);
    return BK_OK;
}

bk_err_t doorbell_isp_snapshot_capture(bk_snapshot_image_t *out_image)
{
    db_device_info_t *info = db_device_info;
    bk_snapshot_config_t cfg = {0};
    avdk_err_t ret;

    if (out_image == NULL)
    {
        return BK_ERR_PARAM;
    }

    os_memset(out_image, 0, sizeof(*out_image));

    if (info == NULL)
    {
        LOGE("%s, info not init\n", __func__);
        return BK_FAIL;
    }

    if (info->video_enable == false)
    {
        LOGE("%s, video not open, turn on camera first\n", __func__);
        return BK_FAIL;
    }

    if (info->camera_id == UVC_DEVICE_ID)
    {
        LOGE("%s, UVC path not supported\n", __func__);
        return BK_ERR_NOT_SUPPORT;
    }

    if (info->isp_handle == NULL || info->encode_handle == NULL || info->h264e_bond == NULL)
    {
        LOGE("%s, isp/h264 pipeline not ready\n", __func__);
        return BK_FAIL;
    }

    cfg.source_handle = info->isp_handle;
    cfg.h264_handle = info->encode_handle;
    cfg.h264_bond = &info->h264e_bond;
    cfg.jpeg_quality = 5;
    cfg.timeout_ms = 3000;

    ret = bk_snapshot_capture(&cfg, out_image);
    if (ret != AVDK_ERR_OK || out_image->size == 0)
    {
        LOGE("%s, bk_snapshot_capture failed ret=%d size=%u\n", __func__, ret, out_image->size);
        return BK_FAIL;
    }

    LOGI("%s, ok size=%u %ux%u\n", __func__, out_image->size, out_image->width, out_image->height);
    return BK_OK;
}
#endif

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


    ret = app_mipi_lcd_turn_on(config);
    if (ret != BK_OK)
    {
        LOGE("%s, app_mipi_lcd_turn_on failed, ret = %d\n", __func__, ret);
        return ret;
    }

    info->lcd_enable = true;

    if (info->video_enable && info->lcd_enable)
    {
        ret = gpu_pipeline_attach(info);
        if (ret != BK_OK)
        {
            LOGE("%s, gpu_pipeline_attach failed, ret = %d\n", __func__, ret);
            goto error;
        }
    }

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
    ntwk_trans_chan_abort(NTWK_TRANS_CHAN_VIDEO, true);
    rtos_get_semaphore(&s_db_trans_cfg->sem, BEKEN_NEVER_TIMEOUT);

    bk_wifi_set_wifi_media_mode(false);

    bk_wifi_set_video_quality(WIFI_VIDEO_QUALITY_HD);
    s_db_trans_cfg->transfer_thread = NULL;
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

        /* Self-test loopback: when armed, feed a copy of this encoded AU to the
         * downlink decode path (no-op unless "db_selftest downlink on"). */
        doorbell_selftest_downlink_tee_feed(frame->frame, frame->length);

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
    ntwk_trans_chan_abort(NTWK_TRANS_CHAN_VIDEO, false);

    s_db_trans_cfg->img_format = img_format;

    if (rtos_init_semaphore(&s_db_trans_cfg->sem, 1) != BK_OK)
    {
        LOGE("%s rtos_init_semaphore failed\n", __func__);
        goto error;
    }

    /* Transfer task only reads encoded AUs from the PSRAM coded heap and pushes
     * them over TCP; it touches no HSRAM/DMA buffers, so keep its stack in the
     * regular (PSRAM) heap. This is deliberate: at 1080p downlink the HSRAM heap
     * is nearly exhausted by the FLEXA ring + GPU + ISP SP + encoder, and an
     * HSRAM stack here would fail to allocate (transfer_app_task init failed). */
    bk_err_t ret = rtos_create_thread(&s_db_trans_cfg->transfer_thread,
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


