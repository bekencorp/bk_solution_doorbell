#pragma once

#include "doorbell_devices.h"
#include <components/avdk_utils/avdk_error.h>
#include <components/bk_camera_ctlr_types.h>

#ifdef __cplusplus
extern "C" {
#endif


bk_camera_ctlr_handle_t uvc_camera_turn_on(camera_parameters_t *parameters);

avdk_err_t uvc_camera_turn_off(bk_camera_ctlr_handle_t handle);

bk_camera_ctlr_handle_t dvp_camera_turn_on(camera_parameters_t *parameters);

avdk_err_t dvp_camera_turn_off(bk_camera_ctlr_handle_t handle);

int doorbell_camera_open(db_device_info_t *info, camera_parameters_t *parameters);

int doorbell_camera_close(db_device_info_t *info);

#ifdef __cplusplus
}
#endif