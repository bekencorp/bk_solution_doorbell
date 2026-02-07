#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/lcd.h>
#include "doorbell_comm.h"

#include "doorbell_cmd.h"
#include "doorbell_devices.h"
#include "frame/frame_que_v2.h"
#include "modules/wifi.h"
#include "decoder/decoder.h"
#include "camera/camera.h"
#include "gpio_driver.h"
#include <driver/gpio.h>
#include "media_utils.h"
#include "components/bk_video_pipeline/bk_video_pipeline.h"
#include "components/bk_display.h"
#include "driver/pwr_clk.h"
#include "driver/flash.h"
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

extern const lcd_device_t lcd_device_custom_st7701sn;
extern const lcd_device_t lcd_device_custom_st7796s;

db_device_info_t *db_device_info = NULL;


typedef struct
{
    uint8_t enable;
    image_format_t img_format;
    beken_semaphore_t sem;
    beken_thread_t transfer_thread;
} db_trans_cfg_t;
static db_trans_cfg_t *s_db_trans_cfg = NULL;
extern media_debug_t *media_debug;


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


int doorbell_get_supported_camera_devices(int opcode)
{
    db_evt_head_t *evt = os_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
    char *p = (char *)(evt + 1);

    evt->opcode = opcode;
    evt->status = EVT_STATUS_OK;
    evt->flags = EVT_FLAGS_CONTINUE;

    LOGD("DBCMD_GET_CAMERA_SUPPORTED_DEVICES\n");

    os_memset(p, 0, DEVICE_RESPONSE_SIZE);

    sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"UVC\", \"ppi\":[\"%dX%d\"]}",
            "UVC",
            UVC_DEVICE_ID,
            0,
            0);
    evt->length = CHECK_ENDIAN_UINT16(strlen(p));
    evt->flags = EVT_FLAGS_COMPLETE;

    ntwk_trans_ctrl_send((uint8_t *)evt, sizeof(db_evt_head_t) + evt->length);

    os_free(evt);

    return 0;
}

int doorbell_get_supported_lcd_devices(int opcode)
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

            ntwk_trans_ctrl_send((uint8_t *)evt, sizeof(db_evt_head_t) + evt->length);
        }
    }

    os_free(evt);

    return 0;
}

int doorbell_get_lcd_status(int opcode)
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

    ntwk_trans_ctrl_send((uint8_t *)evt, sizeof(db_evt_head_t) + evt->length);

    os_free(evt);

    return 0;
}

static void doorbell_send_flash_op_state_callback(uint32_t state)
{
    db_device_info_t *info = db_device_info;

    if (info == NULL || info->handle == NULL)
    {
        return;
    }

    if (state)
    {
        bk_camera_suspend(info->handle);
    }
    else
    {
        bk_camera_resume(info->handle);
    }
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

    if (info->handle)
    {
        LOGE("%s, already open %d\n", __func__, __LINE__);
        ret = BK_OK;
        return ret;
    }

#if (defined(CONFIG_BT_REUSE_MEDIA_MEMORY) && defined(CONFIG_BLUETOOTH_AP))
    bk_bluetooth_deinit();
#endif

    ret = frame_queue_v2_init_all();
    if (ret != BK_OK)
    {
        LOGE("%s, %d frame_queue_init_all fail\n", __func__, __LINE__);
        return ret;
    }

    ret =  doorbell_camera_open(info, parameters);
    if (ret != BK_OK)
    {
        LOGE("%s fail\n", __func__);
        return ret;
    }

    mb_flash_register_op_camera_notify(doorbell_send_flash_op_state_callback);

    LOGD("%s success\n", __func__);

    return ret;
}

int doorbell_camera_turn_off(void)
{
    int ret = BK_OK;

    db_device_info_t *info = db_device_info;
    if (info == NULL || info->handle == NULL)
    {
        LOGE("%s, already close %d\n", __func__, __LINE__);
        return ret;
    }

    ret = doorbell_camera_close(info);
    if (ret != BK_OK)
    {
        LOGW("%s fail\n", __func__);
        return ret;
    }

    mb_flash_unregister_op_camera_notify();

    info->handle = NULL;

    if (info->display_ctlr_handle == NULL)
    {
        frame_queue_v2_clear_all();
    }

    //uint32_t free_count, ready_count, total_malloc, total_complete, total_free;

    //frame_queue_v2_get_stats(IMAGE_MJPEG, &free_count, &ready_count, &total_malloc, &total_complete, &total_free);

    //frame_queue_v2_get_stats(IMAGE_H264, &free_count, &ready_count, &total_malloc, &total_complete, &total_free);

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

    if (info->handle == NULL)
    {
        LOGE("%s: camera not open!\n", __func__);
        return BK_FAIL;
    }

    frame_queue_v2_register_consumer(info->transfer_format, CONSUMER_TRANSMISSION);

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

    if (info->handle == NULL)
    {
        LOGE("%s: camera not open!\n", __func__);
        return ret;
    }

    ret = doorbell_devices_stop();
    if (ret == BK_OK)
    {
        LOGD("%s, success\n", __func__);
    }

    frame_queue_v2_unregister_consumer(info->transfer_format, CONSUMER_TRANSMISSION);

#if (CONFIG_INTEGRATION_DOORBELL_CS2)
   // ntwk_trans_cs2_video_timer_deinit();
#endif

    return ret;
}

int doorbell_display_turn_on(display_parameters_t *parameters)
{
    int ret = BK_FAIL;

    db_device_info_t *info = db_device_info;

    LOGD("%s, id: %d, rotate: %d fmt: %d\n", __func__, parameters->id, parameters->rotate_angle, parameters->pixel_format);

    if (frame_queue_v2_init_all() != BK_OK)
    {
        LOGE("%s, %d frame_queue_init_all fail\n", __func__, __LINE__);
        return EVT_STATUS_ERROR;
    }

    if (info->lcd_id != 0 || info->lcd_device != NULL)
    {
        LOGD("%s, id: %d already open\n", __func__, parameters->id);
        return EVT_STATUS_ALREADY;
    }

#if (defined(CONFIG_BT_REUSE_MEDIA_MEMORY) && defined(CONFIG_BLUETOOTH_AP))
    bk_bluetooth_deinit();
#endif

    const lcd_device_t *device = (const lcd_device_t *)get_lcd_device_by_id(parameters->id);
    if ((uint32_t)device == BK_FAIL || device == NULL)
    {
        LOGD("%s, could not find device id: %d\n", __func__, parameters->id);
        return EVT_STATUS_ERROR;
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
        ret = bk_display_rgb_new(&info->display_ctlr_handle, &rgb_ctlr_config);
    }
    if (device->type == LCD_TYPE_MCU8080)
    {
        bk_display_mcu_ctlr_config_t mcu_ctlr_config = {0};
        mcu_ctlr_config.lcd_device = device;
        ret = bk_display_mcu_new(&info->display_ctlr_handle, &mcu_ctlr_config);
    }
    if (ret != BK_OK)
    {
        LOGE("%s, bk_display_rgb_new failed, ret = %d\n", __func__, ret);
        return ret;
    }

    media_rotate_mode_t rot_mode = NONE_ROTATE;

    if (parameters->pixel_format == 0)
    {
        rot_mode = HW_ROTATE;
    }
    else if (parameters->pixel_format == 1)
    {
        rot_mode = SW_ROTATE;
    }
    else
    {
        rot_mode = NONE_ROTATE;
    }

    if (bk_display_open(db_device_info->display_ctlr_handle) != BK_OK)
    {
        LOGE("%s, display_ctlr_handle open fail\n", __func__);
        goto error;
    }

    ret = mjpeg_decode_open(info, rot_mode, parameters->rotate_angle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, mjpeg_decode_open failed, ret = %d\n", __func__, ret);
        goto error;
    }


    doorbell_lcd_backlight_open(LCD_BACKLIGHT_GPIO);
    info->lcd_id = parameters->id;
    info->lcd_device = device;
    LOGD("%s success\n", __func__);
    return EVT_STATUS_OK;

error:
    mjpeg_decode_close(info);
    bk_display_delete(info->display_ctlr_handle);
    info->display_ctlr_handle = NULL;
    return EVT_STATUS_ERROR;
}

int doorbell_display_turn_off(void)
{
    db_device_info_t *info = db_device_info;

    LOGD("%s, id: %d\n", __func__, info->lcd_id);

    if (info->lcd_id == 0 || info->lcd_device == NULL)
    {
        LOGD("%s, %d already close\n", __func__);
        return EVT_STATUS_ALREADY;
    }

    doorbell_lcd_backlight_close(LCD_BACKLIGHT_GPIO);

    int ret = mjpeg_decode_close(info);

    bk_display_close(info->display_ctlr_handle);
    bk_display_delete(info->display_ctlr_handle);

    info->display_ctlr_handle = NULL;

    info->lcd_id = 0;
    info->lcd_device = NULL;
    doorbell_lcd_ldo_close(LCD_LDO_GPIO);
    if (info->handle == NULL)
    {
        //frame_queue_v2_clear_all();
    }
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
        if (db_device_info->video_pipeline_handle)
        {
            bk_video_pipeline_delete(db_device_info->video_pipeline_handle);
            db_device_info->video_pipeline_handle = NULL;
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
    os_free(s_db_trans_cfg);
	s_db_trans_cfg = NULL;

	LOGD("%s, close success!\r\n", __func__);

	return BK_OK;
}

static void doorbell_devices_task_entry(beken_thread_arg_t data)
{
    db_trans_cfg_t *cfg = (db_trans_cfg_t *)data;
    frame_buffer_t *frame = NULL;
    cfg->enable = true;
    rtos_set_semaphore(&cfg->sem);
    uint32_t before = 0, after = 0;
    uint8_t log_enable = 0;

    while (cfg->enable)
    {
        frame = frame_queue_v2_get_frame(cfg->img_format, CONSUMER_TRANSMISSION, 50);
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
            LOGD("%s, frame sequence %d\n", __func__, frame->sequence);
        }

        log_enable = 0;
        before = get_current_timestamp();
        media_debug->begin_trs = true;
        media_debug->end_trs = false;

        ntwk_trans_video_send((uint8_t *)frame, frame->length,cfg->img_format);

        media_debug->end_trs = true;
        media_debug->begin_trs = false;

        after = get_current_timestamp();

        media_debug->meantimes += (after - before);
        media_debug->fps_wifi++;
        media_debug->wifi_kbps += frame->length;

        frame_queue_v2_release_frame(cfg->img_format, CONSUMER_TRANSMISSION, frame);

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
        LOGE("% malloc failed\r\n", __func__);
        return BK_ERR_NO_MEM;
    }

    memset(s_db_trans_cfg, 0, sizeof(db_trans_cfg_t));

    s_db_trans_cfg->img_format = img_format;

    if (rtos_init_semaphore(&s_db_trans_cfg->sem, 1) != BK_OK)
    {
        LOGE("%s rtos_init_semaphore failed\n", __func__);
        goto error;
    }

// need create task to read frame
bk_err_t ret = rtos_create_thread(&s_db_trans_cfg->transfer_thread,
                                BEKEN_DEFAULT_WORKER_PRIORITY,
                                "trs_task",
                                (beken_thread_function_t)doorbell_devices_task_entry,
                                2560,
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


