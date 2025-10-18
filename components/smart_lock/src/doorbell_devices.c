#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/lcd.h>
#include "doorbell_comm.h"
#include "doorbell_transmission.h"
#include "doorbell_cmd.h"
#include "doorbell_video_device.h"
#include "doorbell_devices.h"
#include "doorbell_frame_que.h"
#include "gpio_driver.h"
#include <driver/gpio.h>
#include "media_app.h"
#include "frame_buffer.h"
#include "media_utils.h"
#include "components/bk_video_pipeline/bk_video_pipeline.h"
#include "components/bk_display.h"
#include "driver/pwr_clk.h"
#if CONFIG_BLUETOOTH_AP
#include "components/bluetooth/bk_dm_bluetooth.h"
#endif


#define TAG "db-device"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define DB_SAMPLE_RARE_8K (8000)
#define DB_SAMPLE_RARE_16K (16000)

#define LCD_BACKLIGHT_GPIO    (GPIO_7)
#define LCD_LDO_GPIO          (GPIO_13)

typedef enum
{
    LCD_STATUS_CLOSE,
    LCD_STATUS_OPEN,
    LCD_STATUS_UNKNOWN,
} lcd_status_t;

extern const dvp_sensor_config_t **get_sensor_config_devices_list(void);
extern int get_sensor_config_devices_num(void);

static camera_type_t curr_cam_type = UNKNOW_CAMERA;

extern const lcd_device_t lcd_device_custom_st7701sn;
extern const lcd_device_t lcd_device_custom_st7796s;

db_device_info_t *db_device_info = NULL;


static avdk_err_t doorbell_lcd_backlight_open(uint8_t bl_io)
{
    gpio_dev_unmap(bl_io);
    BK_LOG_ON_ERR(bk_gpio_enable_output(bl_io));
    BK_LOG_ON_ERR(bk_gpio_pull_up(bl_io));
    bk_gpio_set_output_high(bl_io);
    return AVDK_ERR_OK;
}

static avdk_err_t doorbell_lcd_backlight_close(uint8_t bl_io)
{
    BK_LOG_ON_ERR(bk_gpio_pull_down(bl_io));
    bk_gpio_set_output_low(bl_io);
    return AVDK_ERR_OK;
}

static avdk_err_t doorbell_lcd_ldo_open(uint8_t ldo_io)
{
    bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, ldo_io, GPIO_OUTPUT_STATE_HIGH);
    return AVDK_ERR_OK;
}

static avdk_err_t doorbell_lcd_ldo_close(uint8_t ldo_io)
{
    bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, ldo_io, GPIO_OUTPUT_STATE_LOW);
    return AVDK_ERR_OK;
}



int doorbell_get_ppis(char *ppi, int capability, int size)
{
    int ret = 0;
    strcat(ppi, "[");

    if (capability & PPI_CAP_320X240)
    {
        strcat(ppi, " \"320X240\",");
    }

    if (capability & PPI_CAP_320X480)
    {
        strcat(ppi, " \"320X480\",");
    }

    if (capability & PPI_CAP_480X272)
    {
        strcat(ppi, " \"480X272\",");
    }

    if (capability & PPI_CAP_480X320)
    {
        strcat(ppi, " \"480X320\",");
    }

    if (capability & PPI_CAP_640X480)
    {
        strcat(ppi, " \"640X480\",");
    }

    if (capability & PPI_CAP_480X800)
    {
        strcat(ppi, " \"480X800\",");
    }

    if (capability & PPI_CAP_800X480)
    {
        strcat(ppi, " \"800X480\",");
    }

    if (capability & PPI_CAP_800X600)
    {
        strcat(ppi, " \"800X600\",");
    }

    if (capability & PPI_CAP_864X480)
    {
        strcat(ppi, " \"864X480\",");
    }

    if (capability & PPI_CAP_1024X600)
    {
        strcat(ppi, " \"1024X600\",");
    }

    if (capability & PPI_CAP_1280X720)
    {
        strcat(ppi, " \"1280X720\",");
    }

    if (capability & PPI_CAP_1600X1200)
    {
        strcat(ppi, " \"1600X1200\",");
    }

    if (capability & PPI_CAP_480X480)
    {
        strcat(ppi, " \"480X480\",");
    }

    if (capability & PPI_CAP_720X288)
    {
        strcat(ppi, " \"720X288\",");
    }

    if (capability & PPI_CAP_720X576)
    {
        strcat(ppi, " \"720X576\",");
    }

    if (capability & PPI_CAP_480X854)
    {
        strcat(ppi, " \"480X854\",");
    }

    ret = strlen(ppi);

    ppi[ret - 1] = ']';

    return ret;
}

int doorbell_get_supported_lcd_devices(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb)
{
    uint32_t i, size;
    size = get_lcd_devices_num();//media_app_get_lcd_devices_num();
    const lcd_device_t **device = get_lcd_devices_list();//media_app_get_lcd_devices_list();
    db_evt_head_t *evt = os_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
    char *p = (char *)(evt + 1);

    evt->opcode = opcode;
    evt->status = EVT_STATUS_OK;
    evt->flags = EVT_FLAGS_CONTINUE;

    LOGD("DBCMD_GET_LCD_SUPPORTED_DEVICES\n");

    if ((uint32_t)device != kGeneralErr && device != NULL)
    {
        for (i = 0; i < size; i++)
        {
            os_memset(p, 0, DEVICE_RESPONSE_SIZE);

            LOGV("lcd: %s, ppi: %uX%u\n", device[i]->name, device[i]->width, device[i]->height);
            sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"%s\", \"ppi\":\"%uX%u\"}",
                    device[i]->name,
                    device[i]->id,
                    device[i]->type == LCD_TYPE_RGB565 ? "rgb" : "mcu",
                    device[i]->width,
                    device[i]->height);

            LOGD("dump: %s\n", p);

            evt->length = CHECK_ENDIAN_UINT16(strlen(p));

            if (i == size - 1)
            {
                evt->flags = EVT_FLAGS_COMPLETE;
            }

            doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);
        }
    }

    os_free(evt);

    return 0;
}

int doorbell_get_lcd_status(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb)
{
    uint32_t lcd_status = db_device_info->display_ctlr_handle ? LCD_STATUS_OPEN : LCD_STATUS_CLOSE;

    db_evt_head_t *evt = os_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
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

    doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);

    os_free(evt);

    return 0;
}

static bk_err_t jpeg_complete(bk_err_t result, frame_buffer_t *out_frame)
{
    bk_err_t ret = BK_OK;
    LOGV("----%s, %p, %p\n", __func__, out_frame, out_frame->frame);
    frame_queue_free(IMAGE_MJPEG, out_frame);
    return ret;
}

static frame_buffer_t *jpeg_read(uint32_t timeout_ms)
{
    return frame_queue_get_frame(IMAGE_MJPEG, timeout_ms);
}

static const jpeg_callback_t jpeg_cbs = {
    .read = jpeg_read,
    .complete = jpeg_complete,
};

static bk_err_t display_frame_free_cb(void *frame)
{
    frame_buffer_display_free((frame_buffer_t *)frame);
    return BK_OK;
}

static bk_err_t decode_complete(dec_end_type_t format_type, bk_err_t result, frame_buffer_t *out_frame)
{
    bk_err_t ret = BK_OK;

    if (result != BK_OK)
    {
        display_frame_free_cb(out_frame);
        return BK_OK;
    }
    if (out_frame == NULL)
    {
        return BK_OK;
    }

    if (format_type == HW_DEC_END)
    {
        if (db_device_info->display_ctlr_handle)
        {
            ret = bk_display_flush(db_device_info->display_ctlr_handle, (void *)out_frame, display_frame_free_cb);
            if (ret != BK_OK)
            {
                display_frame_free_cb(out_frame);
            }
        }
        else
        {
            display_frame_free_cb(out_frame);
        }


        return ret;
    }
    else if (format_type == SW_DEC_END)
    {
        if (db_device_info->display_ctlr_handle)
        {
            ret = bk_display_flush(db_device_info->display_ctlr_handle, (void *)out_frame, display_frame_free_cb);
            if (ret != BK_OK)
            {
                display_frame_free_cb(out_frame);
            }
        }
        else
        {
            display_frame_free_cb(out_frame);
        }
        return ret;
    }
    return ret;
}

static frame_buffer_t *decode_malloc(uint32_t size)
{
    frame_buffer_t *frame = NULL;
    frame = frame_buffer_display_malloc(size);
    return frame;
}

static bk_err_t decode_free(frame_buffer_t *frame)
{
    frame_buffer_display_free(frame);
    return BK_OK;
}

static const decode_callback_t decode_cbs = {
    .malloc = decode_malloc,
    .free = decode_free,
    .complete = decode_complete,
};


static frame_buffer_t *h264e_frame_malloc(uint32_t size)
{
    return frame_queue_malloc(IMAGE_H264, size);
}

static void h264e_frame_complete(frame_buffer_t *frame, int result)
{
    if (result != AVDK_ERR_OK)
    {
        frame_queue_free(IMAGE_H264, frame);
    }
    else
    {
        frame_queue_complete(IMAGE_H264, frame);
    }
}

static const bk_h264e_callback_t doorbell_h264e_cbs = {
    .malloc = h264e_frame_malloc,
    .complete = h264e_frame_complete,
};

int doorbell_h264_encode_turn_on(camera_parameters_t *parameters)
{
    bk_err_t ret = BK_FAIL;
    bk_video_pipeline_config_t video_pipeline_config = {0};
    bk_video_pipeline_h264e_config_t h264e_config = {0};

    video_pipeline_config.jpeg_cbs = &jpeg_cbs;
    video_pipeline_config.decode_cbs = &decode_cbs;
    if (db_device_info->video_pipeline_handle == NULL)
    {
        ret = bk_video_pipeline_new(&db_device_info->video_pipeline_handle, &video_pipeline_config);
        if (ret != BK_OK)
        {
            LOGE("%s bk_video_pipeline_new failed\n", __func__);
            return ret;
        }
    }

    h264e_config.width = parameters->width;
    h264e_config.height = parameters->height;
    h264e_config.fps = FPS30;
    h264e_config.sw_rotate_angle = parameters->rotate;
    h264e_config.h264e_cb = &doorbell_h264e_cbs;
    ret = bk_video_pipeline_open_h264e(db_device_info->video_pipeline_handle, &h264e_config);
    if (ret != BK_OK)
    {
        LOGE("%s bk_video_pipeline_open_h264e failed\n", __func__);
    }

    return ret;
}

int doorbell_camera_turn_on(camera_parameters_t *parameters)
{
    bk_err_t ret = BK_FAIL;
    LOGD("%s, id: %d, %d X %d, format: %d, Protocol: %d\n", __func__,
         parameters->id, parameters->width, parameters->height,
         parameters->format, parameters->protocol);

    ret = frame_queue_init_all();
    if (ret != BK_OK)
    {
        LOGE("%s, %d frame_queue_init_all fail\n", __func__, __LINE__);
        return ret;
    }

#if (CONFIG_BT_REUSE_MEDIA_MEMORY && CONFIG_BLUETOOTH_AP)
    bk_bluetooth_deinit();
#endif

    if (parameters->id == UVC_DEVICE_ID)
    {
        curr_cam_type = UVC_CAMERA;
        ret = doorbell_uvc_camera_turn_on(parameters);
        if (ret == BK_OK)
        {
            ret = doorbell_h264_encode_turn_on(parameters);
        }

        ret = doorbell_set_h264_encode_handle(db_device_info->video_pipeline_handle);
    }
    else
    {
        curr_cam_type = DVP_CAMERA;
        ret = doorbell_dvp_camera_turn_on(parameters);
    }

    if (ret != BK_OK)
    {
        LOGE("%s, %d doorbell_camera_turn_on fail\n", __func__, __LINE__);
        return ret;
    }

    if (curr_cam_type == UVC_CAMERA)
    {
        ret = doorbell_h264_encode_turn_on(parameters);
        if (ret != BK_OK)
        {
            return ret;
        }
    }

    if (db_device_info->display_ctlr_handle != NULL)
    {
        bk_video_pipeline_decode_config_t decode_config = {0};

        decode_config.rotate_angle = parameters->rotate;
        ret = bk_video_pipeline_open_rotate(db_device_info->video_pipeline_handle, &decode_config);
        if (ret != BK_OK)
        {
            LOGE("%s bk_video_pipeline_open_rotate failed\n", __func__);
            return ret;
        }
    }

    LOGD("%s success\n", __func__);

    return ret;
}

int doorbell_camera_turn_off(void)
{
    int ret = BK_FAIL;

    if (curr_cam_type == UVC_CAMERA)
    {
        ret = bk_video_pipeline_close_h264e(db_device_info->video_pipeline_handle);
        if (ret != BK_OK)
        {
            LOGE("%s bk_video_pipeline_close_h264e failed\n", __func__);
        }
        LOGD("%s h264_pipeline close\n", __func__);
    }

    ret = doorbell_camera_device_turn_off();

    if (ret == BK_OK)
    {
        LOGD("%s success\n", __func__);
    }

    frame_queue_clear_all();

    return ret;
}

int doorbell_display_turn_on(display_parameters_t *parameters)
{
    int ret = BK_FAIL;

    LOGD("%s, id: %d, rotate: %d fmt: %d\n", __func__, parameters->id, parameters->rotate_angle, parameters->pixel_format);

    if (frame_queue_init_all() != BK_OK)
    {
        LOGE("%s, %d frame_queue_init_all fail\n", __func__, __LINE__);
        return EVT_STATUS_ERROR;
    }

    if (db_device_info->lcd_id != 0 || db_device_info->lcd_device != NULL)
    {
        LOGD("%s, id: %d already open\n", __func__, parameters->id);
        return EVT_STATUS_ALREADY;
    }
    const lcd_device_t *device = (const lcd_device_t *)get_lcd_device_by_id(parameters->id);
    if ((uint32_t)device == BK_FAIL || device == NULL)
    {
        LOGD("%s, could not find device id: %d\n", __func__, parameters->id);
        //return EVT_STATUS_ERROR;
    }
    if (device == NULL)
    {
        //device = &lcd_device_custom_st7701sn;  // custom lcd device
        device = &lcd_device_custom_st7796s;
        LOGD("%s, lcd device use custom: %s\n", __func__, device->name);
    }
    doorbell_lcd_ldo_open(LCD_LDO_GPIO);
    if (device->type == LCD_TYPE_RGB)
    {
        bk_display_rgb_ctlr_config_t rgb_ctlr_config = {0};
        rgb_ctlr_config.lcd_device = device;
        rgb_ctlr_config.clk_pin = GPIO_0;
        rgb_ctlr_config.cs_pin = GPIO_12;
        rgb_ctlr_config.sda_pin = GPIO_1;
        rgb_ctlr_config.rst_pin = GPIO_6;
        ret = bk_display_rgb_new(&db_device_info->display_ctlr_handle, &rgb_ctlr_config);
    }
    if (device->type == LCD_TYPE_MCU8080)
    {
        bk_display_mcu_ctlr_config_t mcu_ctlr_config = {0};
        mcu_ctlr_config.lcd_device = device;
        ret = bk_display_mcu_new(&db_device_info->display_ctlr_handle, &mcu_ctlr_config);
    }
    if (ret != BK_OK)
    {
        LOGE("%s, bk_display_rgb_new failed, ret = %d\n", __func__, ret);
        return ret;
    }

    if (db_device_info->video_pipeline_handle == NULL)
    {
        bk_video_pipeline_config_t video_pipeline_config = {0};
        video_pipeline_config.jpeg_cbs = &jpeg_cbs;
        video_pipeline_config.decode_cbs = &decode_cbs;
        ret = bk_video_pipeline_new(&db_device_info->video_pipeline_handle, &video_pipeline_config);
        if (ret != BK_OK)
        {
            LOGE("%s, bk_video_pipeline_new fail\n", __func__);
            goto error;
        }
    }
    bk_video_pipeline_decode_config_t decode_config = {0};
    if (parameters->pixel_format == 0)
    {
        decode_config.rotate_mode = HW_ROTATE;
    }
    else if (parameters->pixel_format == 1)
    {
        decode_config.rotate_mode = SW_ROTATE;
    }
    else
    {
        decode_config.rotate_mode = NONE_ROTATE;
    }
    decode_config.rotate_angle = parameters->rotate_angle;  //0,90,180,270

    ret = bk_video_pipeline_open_rotate(db_device_info->video_pipeline_handle, &decode_config);
    if (ret != BK_OK)
    {
        LOGE("%s, video_pipeline_handle open fail\n", __func__);
        goto error;
    }

    if (bk_display_open(db_device_info->display_ctlr_handle) != BK_OK)
    {
        LOGE("%s, display_ctlr_handle open fail\n", __func__);
        goto error;
    }
    doorbell_lcd_backlight_open(LCD_BACKLIGHT_GPIO);
    db_device_info->lcd_id = parameters->id;
    db_device_info->lcd_device = device;
    LOGD("%s success\n", __func__);
    return EVT_STATUS_OK;

    error:
    bk_video_pipeline_close_rotate(db_device_info->video_pipeline_handle);
    bk_display_delete(db_device_info->display_ctlr_handle);
    db_device_info->display_ctlr_handle = NULL;
    return EVT_STATUS_ERROR;

}
int doorbell_display_turn_off(void)
{
    int ret = BK_FAIL;

    LOGD("%s, id: %d\n", __func__, db_device_info->lcd_id);

    if (db_device_info->lcd_id == 0 || db_device_info->lcd_device == NULL)
    {
        LOGD("%s, %d already close\n", __func__);
        return EVT_STATUS_ALREADY;
    }
    doorbell_lcd_backlight_close(LCD_BACKLIGHT_GPIO);
    ret = bk_video_pipeline_close_rotate(db_device_info->video_pipeline_handle);
    if (ret != BK_OK)
    {
        LOGE("%s %d, video_pipeline_handle close fail\n", __func__, __LINE__);
        return EVT_STATUS_ERROR;
    }

    bk_display_close(db_device_info->display_ctlr_handle);
    bk_display_delete(db_device_info->display_ctlr_handle);

    db_device_info->display_ctlr_handle = NULL;

    db_device_info->lcd_id = 0;
    db_device_info->lcd_device = NULL;
    doorbell_lcd_ldo_close(LCD_LDO_GPIO);
    LOGD("%s success\n", __func__);
    return 0;
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
        if (db_device_info->video_pipeline_handle)
        {
            bk_video_pipeline_delete(db_device_info->video_pipeline_handle);
            db_device_info->video_pipeline_handle = NULL;
        }
        os_free(db_device_info);
        db_device_info = NULL;
    }
}
