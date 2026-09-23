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

#ifndef __DOORBELL_CALL_CTRL_H__
#define __DOORBELL_CALL_CTRL_H__

#include <common/bk_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_DOOR_ACCESS_CALL_FLOW

/**
 * @file doorbell_call_ctrl.h
 *
 * Indoor-display incoming-call control (CONFIG_DOOR_ACCESS_CALL_FLOW).
 *
 * The smart_intercom two-way video path is split into an *invite / answer /
 * hangup* state machine so the local device drives the call. The App-facing
 * protocol is UNCHANGED from video_intercom: doorbell.videoIntercom.turnOn is
 * answered with a plain success result on answer, or an error on decline / cancel
 * / start failure. Only the reply *timing* differs - it is deferred (not sent on
 * receipt) so a local decline surfaces to the App as a failed turnOn:
 *
 *   App sends doorbell.videoIntercom.turnOn
 *        -> DOORBELL_CALL_EVT_INCOMING (device shows the ring/incoming screen,
 *           media NOT started, the turnOn RPC reply is deferred)
 *   Local user answers (touch / key)  -> doorbell_call_answer()
 *        -> uplink + downlink media start, DOORBELL_CALL_EVT_ACTIVE,
 *           the pending turnOn is answered with a success result
 *   Local user declines while ringing (touch / key / OSD) -> doorbell_call_hangup()
 *        -> DOORBELL_CALL_EVT_ENDED, the pending turnOn is answered with an error
 *   Local user hangs up an active call -> doorbell_call_hangup()
 *        -> media stop, DOORBELL_CALL_EVT_ENDED (turnOn already answered)
 *   App sends doorbell.videoIntercom.turnOff, or the peer disconnects
 *        -> media stop, DOORBELL_CALL_EVT_ENDED (a still-ringing invite is
 *           answered with an error)
 *
 * The event callback runs on the caller's thread (RPC / network thread for
 * INCOMING and remote-ended, or whatever thread invoked answer/hangup). It must
 * be light and must NOT block or do heavy LVGL work inline; the local UI layer
 * marshals the work onto its own worker thread.
 */

typedef enum
{
    DOORBELL_CALL_IDLE = 0,   /**< no call */
    DOORBELL_CALL_RINGING,    /**< invite received, waiting for local answer */
    DOORBELL_CALL_ACTIVE,     /**< answered, two-way media running */
} doorbell_call_state_t;

typedef enum
{
    DOORBELL_CALL_EVT_INCOMING = 0, /**< App invited a call (videoIntercom.turnOn) */
    DOORBELL_CALL_EVT_ACTIVE,       /**< local answer succeeded, media up */
    DOORBELL_CALL_EVT_ENDED,        /**< call ended (local/remote hangup or disconnect) */
} doorbell_call_event_t;

typedef void (*doorbell_call_event_cb_t)(doorbell_call_event_t evt, void *user);

/**
 * @brief Register the local call event callback (pass cb = NULL to unregister).
 *        Runs on the caller's thread; keep it light (see file header).
 */
void doorbell_call_set_event_cb(doorbell_call_event_cb_t cb, void *user);

/** @brief Current call state. */
doorbell_call_state_t doorbell_call_state_get(void);

/**
 * @brief Local user answers the incoming call.
 *
 * Only valid in DOORBELL_CALL_RINGING; starts uplink + downlink media using the
 * parameters carried by the invite, moves to DOORBELL_CALL_ACTIVE and answers the
 * App's pending turnOn with a success result (an error if media start fails).
 * No-op (BK_ERR_STATE) in any other state.
 *
 * IMPORTANT: the caller MUST have already yielded the panel/HSRAM to the video
 * pipeline (LVGL torn down) before calling this, because media start allocates
 * from the shared HSRAM pool.
 *
 * @return BK_OK on success.
 */
bk_err_t doorbell_call_answer(void);

/**
 * @brief Local user hangs up (or rejects a ringing call).
 *
 * If ringing, the App's pending turnOn is answered with an error (decline). If
 * active, media is stopped (the turnOn was already answered on pickup). Moves to
 * DOORBELL_CALL_IDLE. No-op if already idle.
 *
 * @return BK_OK on success.
 */
bk_err_t doorbell_call_hangup(void);

#endif /* CONFIG_DOOR_ACCESS_CALL_FLOW */

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_CALL_CTRL_H__ */
