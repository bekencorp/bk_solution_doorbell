#ifndef __DOORBELL_DEVICES_H__
#define __DOORBELL_DEVICES_H__

#include <components/bk_encode/bk_h264_encode_types.h>
#include <components/bk_decode/bk_jpeg_decode_types.h>
#include <components/bk_flexa_bond.h>

#include "app_display_types.h"

typedef struct
{
    uint16_t id;
    uint16_t width;
    uint16_t height;
    uint16_t format;
    uint16_t protocol;
    uint16_t rotate;
} camera_parameters_t;

typedef struct
{
    uint16_t id;
    uint16_t rotate_angle;
    uint8_t  pixel_format;
} display_parameters_t;

typedef struct
{
    uint8_t lcd_enable;
    uint8_t video_enable;
    uint8_t audio_enable;
    uint8_t lcd_id;
    uint16_t camera_id; // UVC_DEVICE_ID, MIPI_DEVICE_ID
    uint16_t transfer_format;
    const void *lcd_device;
    const void *sensor_device;
    void *h264e_bond;
    void *gpu_bond;
    bk_h264_encode_ctlr_handle_t encode_handle;
    bk_jpeg_decode_ctlr_handle_t decode_handle;
    bk_gpu_ctlr_handle_t gpu_handle;
    void *isp_handle;
} db_device_info_t;

int doorbell_get_supported_camera_devices(int opcode);
int doorbell_get_supported_lcd_devices(int opcode);
int doorbell_get_lcd_status(int opcode);
void doorbell_devices_deinit(void);
int doorbell_devices_init(void);

int doorbell_camera_turn_on(camera_parameters_t *parameters);
int doorbell_camera_turn_off(void);

int doorbell_display_turn_on(display_board_config_t *config);
int doorbell_display_turn_off(void);

bk_err_t doorbell_devices_start(uint16_t img_format);
bk_err_t doorbell_devices_stop(void);
/**
 * @brief      开启视频传输功能
 *
 * @return     int 操作结果
 * BK_OK: 成功
 * BK_ERR: 失败
 * BK_ERR_NOT_SUPPORT: 不支持该操作
 */
int doorbell_video_transfer_turn_on(void);

/**
 * @brief      关闭视频传输功能
 *
 * @return     int 操作结果
 * BK_OK: 成功
 * BK_ERR: 失败
 * BK_ERR_NOT_SUPPORT: 不支持该操作
 */
int doorbell_video_transfer_turn_off(void);

#endif
