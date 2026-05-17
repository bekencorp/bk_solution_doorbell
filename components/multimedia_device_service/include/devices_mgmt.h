#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "app_camera_types.h"

#define DEVICES_MGMT_QUEUE_SIZE 10

typedef enum
{
    DEVICES_MGMT_EXIT,
    DEVICES_MGMT_ISP_TURN_ON,
    DEVICES_MGMT_ISP_DVP_TURN_ON,
    DEVICES_MGMT_ISP_MIPI_TURN_ON,
    DEVICES_MGMT_ISP_DUAL_TURN_ON,
    DEVICES_MGMT_ISP_TURN_OFF,

    DEVICES_MGMT_MIPI_LCD_TURN_ON,
    DEVICES_MGMT_MIPI_LCD_TURN_OFF,

    DEVICES_MGMT_ENCODE_ISP_CAMERA_TURN_ON,
    DEVICES_MGMT_ENCODE_ISP_CAMERA_TURN_OFF,
} devices_mgmt_event_t;

typedef enum
{
    DISPLAY_STREAM_ID_INVALID = 0,
    DISPLAY_STREAM_ID_PORT_0_UVC,
    DISPLAY_STREAM_ID_PORT_1_UVC,
    DISPLAY_STREAM_ID_MIPI_CSI,
    DISPLAY_STREAM_ID_DVP,
} display_stream_id_t;

typedef struct
{
    display_stream_id_t  id;
    void *args;
} display_source_t;



typedef struct
{
    uint32_t event;
    uintptr_t param;
    uint32_t result;
    beken_semaphore_t wait;
} devices_mgmt_msg_t;

typedef enum
{
    CODEC_FORMAT_UNKNOW = 0,
    CODEC_FORMAT_G711A = 1,
    CODEC_FORMAT_PCM = 2,
    CODEC_FORMAT_G711U = 3,
} codec_format_t;

typedef enum
{
    AA_UNKNOWN = 0,
    AA_ECHO_DEPTH = 1,
    AA_MAX_AMPLITUDE = 2,
    AA_MIN_AMPLITUDE = 3,
    AA_NOISE_LEVEL = 4,
    AA_NOISE_PARAM = 5,
} audio_acoustics_t;

void devices_mgmt_deinit(void);
int devices_mgmt_init(void);
void devices_cli_init(void);

bk_err_t devices_mgmt_set_display_source(display_stream_id_t id, void *args);
display_source_t *devices_mgmt_get_display_source(void);

bk_err_t devices_mgmt_add_uvc_device(app_uvc_device_t *device, uint8_t port);
app_uvc_device_t *devices_mgmt_get_uvc_device(uint8_t port);
#ifdef __cplusplus
}
#endif
