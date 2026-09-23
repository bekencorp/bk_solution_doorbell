#ifndef __DOORBELL_MULTI_SESSION_MGR_H__
#define __DOORBELL_MULTI_SESSION_MGR_H__

#include <stdbool.h>
#include <common/bk_err.h>
#include "network_transfer.h"
#include "doorbell_devices.h"
#include "doorbell_audio_device.h"

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t doorbell_multi_session_mgr_init(void);
bk_err_t doorbell_multi_session_mgr_deinit(void);
bk_err_t doorbell_multi_session_on_connect(ntwk_session_id_t sid);
bk_err_t doorbell_multi_session_on_disconnect(ntwk_session_id_t sid);

bk_err_t doorbell_multi_session_camera_on(ntwk_session_id_t sid, camera_parameters_t *parameters);
bk_err_t doorbell_multi_session_camera_off(ntwk_session_id_t sid);
bk_err_t doorbell_multi_session_audio_on(ntwk_session_id_t sid, audio_parameters_t *parameters, bool *owner);
bk_err_t doorbell_multi_session_audio_off(ntwk_session_id_t sid);
bk_err_t doorbell_multi_session_lcd_on(ntwk_session_id_t sid);
bk_err_t doorbell_multi_session_lcd_off(ntwk_session_id_t sid);

#ifdef __cplusplus
}
#endif

#endif
