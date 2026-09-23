// Copyright 2020-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __DOORBELL_CALL_H__
#define __DOORBELL_CALL_H__

#include <common/bk_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file doorbell_call.h
 *
 * intercom_indoor_display local call controller (front-end of CONFIG_DOOR_ACCESS_CALL_FLOW).
 *
 * Owns the on-device call UX that sits on top of the smart_intercom call-control
 * core (doorbell_call_ctrl.h):
 *   - shows the incoming-call (来电) screen when the App invites a call,
 *   - plays a local ringtone until the call is answered or ended,
 *   - drives the LVGL <-> video-pipeline handoff on answer / hangup,
 *   - draws the on-video hang-up and lock OSD buttons and hit-tests touches,
 *   - accepts answer / hangup requests from touch and physical keys.
 *
 * All heavy work (LVGL teardown/rebuild, media start/stop) runs on a dedicated
 * worker thread so it is safe to request from the LVGL, touch and key threads.
 */

/**
 * @brief Initialize the call controller: register the smart_intercom call-event
 *        and multimedia-status callbacks, open the touch panel and start the
 *        worker thread. Call once, after doorbell_ui_init().
 */
bk_err_t doorbell_call_ui_init(void);

/**
 * @brief Request answering the current incoming call (from touch/key). Safe to
 *        call from any thread; no-op if there is no ringing call.
 */
void doorbell_call_request_answer(void);

/**
 * @brief Request hanging up / declining the current call (from touch/key/OSD).
 *        Safe to call from any thread; no-op if there is no call.
 */
void doorbell_call_request_hangup(void);

/**
 * @brief Whether a call is ringing or active (used by the key handler to decide
 *        between call actions and normal UI navigation).
 */
bool doorbell_call_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_CALL_H__ */
