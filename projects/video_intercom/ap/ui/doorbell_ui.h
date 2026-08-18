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

#ifndef __DOORBELL_UI_H__
#define __DOORBELL_UI_H__

#include <common/bk_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file doorbell_ui.h
 *
 * video_intercom local UI controller: centralizes LVGL start/stop, page switching,
 * provisioning state, and key navigation. The generated UI (ap/beken_generated) is
 * referenced purely as a UI asset; this controller drives it through the exported
 * bk_lv_tool_ui handle and init_page_* / navigate_to_screen, without modifying the
 * generated code.
 */

typedef enum {
    DB_UI_PAGE_HOME = 0,
    DB_UI_PAGE_PROVISIONING,
    DB_UI_PAGE_SETTINGS,
} doorbell_ui_page_t;

/**
 * @brief Initialize the UI controller (does not start LVGL).
 *
 * Only performs internal state init and registers the provisioning-state callback
 * with doorbell_netcfg. Must be called before doorbell_core_init() so the first
 * provisioning state is not missed.
 */
bk_err_t doorbell_ui_init(void);

/**
 * @brief Boot-media (animation/image) playback-done callback: keep the panel lit,
 *        start LVGL and select the first screen.
 *
 * Provisioned -> home; not provisioned -> provisioning (QR content is injected by
 * the upper layer via doorbell_ui_set_qr).
 */
void doorbell_ui_on_boot_media_done(bk_err_t result, void *user_data);

/**
 * @brief Switch to the given page (lazy page creation + lv_screen_load). Thread-safe.
 */
void doorbell_ui_goto(doorbell_ui_page_t page);

/**
 * @brief User-initiated provisioning (the "provision" key entry).
 *
 * First clears the saved provisioning info (also disconnects the current STA and
 * returns to the unprovisioned state), then switches to the provisioning page and
 * starts BLE provisioning to wait for the phone to re-provision. Lets the user
 * re-provision at any time without erasing and rebooting first.
 */
void doorbell_ui_request_provisioning(void);

/**
 * @brief Inject QR content from the upper layer (the string is encoded as-is).
 *        Thread-safe.
 *
 * Only refreshes the QR widget on the provisioning page; does not assemble any
 * payload format.
 */
void doorbell_ui_set_qr(const char *payload);

/**
 * @brief Update the bottom status text on the provisioning page. Thread-safe.
 */
void doorbell_ui_set_status(const char *text);

/**
 * @brief Override the device-info labels on the provisioning page
 *        (Name/MAC/Model/Protocol). Thread-safe.
 *
 * Passing NULL for any argument leaves that item unchanged. When entering the
 * provisioning page the controller first auto-fills with the device's real values
 * (name and address derived from the BT MAC + default Model/Protocol); call this
 * to override after navigating to the page if custom values are needed.
 */
void doorbell_ui_set_device_info(const char *name, const char *mac,
                                 const char *model, const char *proto);

/**
 * @brief Whether LVGL has started and is ready.
 */
/**
 * @brief Enter/exit the two-way video-call view (LVGL and the video-call pipeline
 *        share the one panel).
 *
 * active=true:  Pause the LVGL rendering task and hand the panel to the two-way
 *               video-call pipeline (decode+compose then flush straight to the LCD),
 *               avoiding two flush paths fighting over the screen; the panel stays
 *               lit, not turned off.
 * active=false: Restore the DPU runtime format to what LVGL needs (ARGB8888 +
 *               decompress; the call pipeline may have changed it), switch back to
 *               the home page and force a full-screen redraw, then resume LVGL
 *               rendering.
 *
 * The screen never blanks during the switch: app_display's refcount holds one level
 * from the boot media (KEEP_ON), so the call's lcd.turnOff only drops the count from
 * 2 to 1 and never actually powers off.
 *
 * Normally driven automatically by doorbell_mm_service_vote's idle<->active edge
 * callback (enters when the phone opens voice/image/display, exits when all are
 * closed); business code need not call it manually. Thread-safe.
 */
void doorbell_ui_set_call_active(bool active);

bool doorbell_ui_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_UI_H__ */
