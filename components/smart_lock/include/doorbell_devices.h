#ifndef __DOORBELL_DEVICES_H__
#define __DOORBELL_DEVICES_H__

#include <components/bk_video_pipeline/bk_video_pipeline_types.h>
#include <components/bk_camera_ctlr.h>
#include "network_transfer.h"
#include "components/bk_display.h"


typedef struct
{
    uint16_t id;
    uint16_t width;
    uint16_t height;
    uint16_t format;
    uint16_t protocol;
    uint16_t rotate;

#ifdef CONFIG_STANDARD_DUALSTREAM
    uint16_t dualstream;
    uint16_t d_width;
    uint16_t d_height;
#endif
} camera_parameters_t;

typedef struct
{
    uint16_t id;
    uint16_t rotate_angle;
    uint8_t  pixel_format;
} display_parameters_t;

typedef struct
{
    uint16_t lcd_id;
    const void *lcd_device;
    bk_video_pipeline_handle_t video_pipeline_handle;
    bk_display_ctlr_handle_t display_ctlr_handle;
    camera_type_t cam_type;
    image_format_t transfer_format;
    bk_camera_ctlr_handle_t handle;
} db_device_info_t;

int doorbell_get_supported_camera_devices(int opcode);
int doorbell_get_supported_lcd_devices(int opcode);
int doorbell_get_lcd_status(int opcode);
void doorbell_devices_deinit(void);
int doorbell_devices_init(void);

int doorbell_camera_turn_on(camera_parameters_t *parameters);
int doorbell_camera_turn_off(void);

int doorbell_display_turn_on(display_parameters_t *parameters);
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
