#pragma once

#include "doorbell_devices.h"
#include <components/bk_display_types.h>

#ifdef __cplusplus
extern "C" {
#endif

int mjpeg_decode_open(db_device_info_t *info, media_rotate_mode_t rot, int angle);

int mjpeg_decode_close(db_device_info_t *info);

void mjpeg_decoder_reset(void);

#ifdef __cplusplus
}
#endif