#ifndef __DOORBELL_DEVICES_H__
#define __DOORBELL_DEVICES_H__

#include "doorbell_transmission.h"
#include "components/bk_display.h"
#include "wifi_transfer.h"
#include <components/bk_video_pipeline/bk_video_pipeline_types.h>
#include <components/bk_camera_ctlr.h>

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
	const void * lcd_device;
	const media_transfer_cb_t *audio_transfer_cb;
	bk_video_pipeline_handle_t video_pipeline_handle;
	bk_display_ctlr_handle_t display_ctlr_handle;
} db_device_info_t;

int doorbell_get_supported_camera_devices(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb);
int doorbell_get_supported_lcd_devices(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb);
int doorbell_get_lcd_status(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb);
void doorbell_devices_deinit(void);
int doorbell_devices_init(void);

int doorbell_camera_turn_on(camera_parameters_t *parameters);
int doorbell_camera_turn_off(void);

int doorbell_display_turn_on(display_parameters_t *parameters);
int doorbell_display_turn_off(void);

#endif
