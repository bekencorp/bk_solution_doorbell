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

/*
 * video_intercom local UI controller implementation.
 *
 * Display ownership: reuses app_display's single display owner; does not create a
 * second DSI bus. After the boot media finishes playing in KEEP_ON mode the panel
 * stays lit and LVGL takes over directly (no blank-screen transition). LVGL stays
 * resident the whole time; page switching uses the generated UI's
 * navigate_to_screen(), rather than the old qr_provisioning which started/stopped
 * on every provisioning.
 *
 * LVGL frame buffers are sent straight to app_display's bk_display controller via
 * lv_vendor's flush_cb, consistent with the SDK reference project
 * projects/lvgl/widgets_v9.
 */

#include <common/bk_include.h>
#include <common/bk_err.h>
#include <os/os.h>
#include <string.h>
#include <stdio.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_display.h>
#include <components/bluetooth/bk_dm_bluetooth.h>

#include "lvgl.h"
#include "lv_vendor.h"
#include "app_display.h"

#include "beken_ui.h"
#include "event_runtime.h"
#include "doorbell_netcfg.h"
#include "doorbell_cmd.h"
#include "doorbell_downlink_video.h"
#include "doorbell_ui.h"

#define TAG "db-ui"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Panel native geometry (HX8399C MIPI 1080x1920, matches ap_main display_board). */
#define DB_UI_PANEL_WIDTH  (1080)
#define DB_UI_PANEL_HEIGHT (1920)

#ifndef CONFIG_LVGL_FRAME_BUFFER_NUM
#define CONFIG_LVGL_FRAME_BUFFER_NUM (2)
#endif

/* Default device-info values on the provisioning page. Name/MAC are derived from the
 * BT MAC in real time when entering the provisioning page; Model/Protocol are fixed
 * product items - change these two macros to adjust (or override at runtime with
 * doorbell_ui_set_device_info). */
#ifndef DB_UI_DEVICE_MODEL
#define DB_UI_DEVICE_MODEL "VDB-X1"
#endif
#ifndef DB_UI_DEVICE_PROTO
#define DB_UI_DEVICE_PROTO "BLE 5.2"
#endif

static volatile bool            s_lvgl_ready;
static volatile bool            s_boot_done;
static volatile bool            s_pending_prov;     /* PROVISIONING event that arrived before LVGL was ready */
static volatile bool            s_pending_success;  /* SUCCESS event that arrived before LVGL was ready */
static volatile bool            s_call_active;      /* Two-way video call in progress: LVGL yields to the video pipeline */

static bk_display_ctlr_handle_t s_lcd_handle;
static void                    *s_frame_buffer[CONFIG_LVGL_FRAME_BUFFER_NUM];

/* Frame buffer currently committed to the panel (last one flushed by LVGL). */
static void * volatile          s_onscreen_frame;
/* On the LVGL -> video-call handoff we keep the last on-screen LVGL frame buffer
 * allocated (instead of freeing it) so the slab does not recycle it into the
 * video pipeline and overwrite it while the DPU is still scanning it out. The
 * panel therefore stays frozen on the last UI frame (no garbage / 花屏) until the
 * compositor composes its first decoded frame. Freed once a new frame replaces it
 * on the panel (see doorbell_ui_flush_cb). */
static void * volatile          s_pinned_frame;

/* -------------------------------------------------------------------------- */
/* LVGL bring-up                                                              */
/* -------------------------------------------------------------------------- */

static void doorbell_ui_flush_cb(void *args, void *frame_buffer, int (*cb)(void *args))
{
    s_onscreen_frame = frame_buffer;
    bk_display_flush(args, frame_buffer, cb);

    /* A pinned pre-call UI frame is superseded on the panel by this fresh commit
     * (rebuilt LVGL after a call). Release it now that the panel no longer needs
     * it. Video-call frames are pushed straight to app_mipi_lcd_flush and never
     * reach this LVGL flush_cb, so during a call the pin simply persists until the
     * UI is rebuilt at call end. */
    if (s_pinned_frame != NULL && frame_buffer != s_pinned_frame)
    {
        void *stale = s_pinned_frame;
        s_pinned_frame = NULL;
        bk_frame_buffer_free(stale);
    }
}

/* Restore the DPU runtime pixel format to what LVGL needs (per the dpu_video config
 * at panel bring-up, usually ARGB8888 + decompress). The boot media / video-call
 * pipeline temporarily rewrite the DPU runtime format to display their own frames and
 * do not restore it; LVGL uses the compressed-frame path, so without restoring, the
 * DPU would interpret LVGL frames in the wrong format -> screen glitch. Called once
 * when start_lvgl first takes over and before returning to LVGL after each call ends. */
static void doorbell_ui_restore_dpu_format(void)
{
    display_board_config_t *db = app_display_board_config_get();

    if (db == NULL || s_lcd_handle == NULL)
    {
        return;
    }

    bk_display_pixel_format_config_t pf = {
        .format     = db->dpu_video.format,
        .decompress = db->dpu_video.decompress,
    };
    if (bk_display_pixel_format_set(s_lcd_handle, &pf) != BK_OK)
    {
        LOGW("restore DPU pixel format failed, screen may glitch\r\n");
    }
}

static void doorbell_ui_free_frame_buffers(void)
{
    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++)
    {
        if (s_frame_buffer[i])
        {
            bk_frame_buffer_free(s_frame_buffer[i]);
            s_frame_buffer[i] = NULL;
        }
    }
}

/* Free every LVGL frame buffer except the one currently on the panel (keep),
 * whose ownership is transferred to the caller (its slot is nulled, not freed).
 * Returns true if keep was found and retained. */
static bool doorbell_ui_free_frame_buffers_except(void *keep)
{
    bool kept = false;

    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++)
    {
        if (s_frame_buffer[i] == NULL)
        {
            continue;
        }
        if (keep != NULL && s_frame_buffer[i] == keep)
        {
            s_frame_buffer[i] = NULL; /* relinquish; caller now owns it */
            kept = true;
            continue;
        }
        bk_frame_buffer_free(s_frame_buffer[i]);
        s_frame_buffer[i] = NULL;
    }
    return kept;
}

/* Fully tear down LVGL, returning all the HSRAM it held to the video-call pipeline.
 *
 * Background: lv_vendor_stop() only ends the rendering task; the ~270KB HSRAM draw
 * buffers, GPU (vg_lite/gpu_driver) resources, and our own frame buffers allocated by
 * lv_vendor_init are not freed. The video-call pipeline's compositor pingpong / ISP
 * flexa also allocate from HSRAM, so merely pausing causes their malloc to fail
 * (bk_get_gpu_output_buffer / ispCoreDevs malloc failed). Entering a call therefore
 * requires a full teardown:
 *   stop (stop task) -> deinit (free draw_buf/GPU/mutex/queue/vnd_data)
 *   -> lv_deinit (destroy all LVGL objects and the core) -> free frame buffers
 *   -> zero the generated-UI pointers.
 * Zeroing bk_lv_tool_ui is mandatory: after lv_deinit its object pointers all dangle,
 * and navigate_to_screen relies on lv_obj_is_valid(*target) to decide whether to
 * reuse; dereferencing a dangling pointer would access freed memory. */
static void doorbell_ui_teardown_lvgl(void)
{
    if (!s_lvgl_ready)
    {
        return;
    }

    lv_vendor_stop();
    lv_vendor_deinit();
    lv_deinit();

    /* Preserve the last UI frame across the handoff: keep the on-screen buffer
     * pinned (do not free it) so the panel keeps showing it - frozen but clean -
     * while the video pipeline allocates from the rest of the (16MB/13MB) slab
     * heaps. Without this the freed buffer is recycled into the downlink slots /
     * compositor pool and overwritten under the still-scanning DPU -> 花屏 until
     * the first remote frame is decoded. A stale pin from a prior call (should be
     * NULL by now) is dropped first to avoid a leak. */
    if (s_pinned_frame != NULL)
    {
        void *stale = s_pinned_frame;
        s_pinned_frame = NULL;
        bk_frame_buffer_free(stale);
    }
    if (doorbell_ui_free_frame_buffers_except(s_onscreen_frame))
    {
        s_pinned_frame = s_onscreen_frame;
    }
    s_onscreen_frame = NULL;

    memset(&bk_lv_tool_ui, 0, sizeof(bk_lv_tool_ui));

    s_lvgl_ready = false;
}

static bk_err_t doorbell_ui_start_lvgl(void)
{
    if (s_lvgl_ready)
    {
        return BK_OK;
    }

    /* The panel was lit by the boot media in KEEP_ON mode; if it is off due to some
     * anomaly, turn it on once more. */
    if (!app_mipi_lcd_state_get())
    {
        if (app_mipi_lcd_turn_on(app_display_board_config_get()) != BK_OK)
        {
            LOGE("app_mipi_lcd_turn_on failed\r\n");
            return BK_FAIL;
        }
    }

    s_lcd_handle = (bk_display_ctlr_handle_t)app_mipi_lcd_handle_get();
    if (s_lcd_handle == NULL)
    {
        LOGE("no lcd handle\r\n");
        return BK_FAIL;
    }

    /* Key: to display JPEG, boot_image_player switched the DPU runtime format to raw
     * NV12 + decompress=false (boot_image_display.c), and KEEP_ON does not restore it.
     * LVGL uses the compressed-frame path (output_compress=true, DPU-side
     * decompress=true); without restoring, the DPU would interpret LVGL's compressed
     * frames as NV12 -> screen glitch. Restore per the panel bring-up dpu_video config. */
    doorbell_ui_restore_dpu_format();

    lv_vnd_config_t cfg = {0};
    cfg.width  = DB_UI_PANEL_WIDTH;
    cfg.height = DB_UI_PANEL_HEIGHT;
    cfg.render_mode = RENDER_PARTIAL_MODE;
    cfg.draw_pixel_size = DB_UI_PANEL_WIDTH * 64 * sizeof(bk_color_t);
    cfg.rotation = ROTATE_NONE;
    cfg.output_compress = true;
    cfg.disp_width  = DB_UI_PANEL_WIDTH;
    cfg.disp_height = DB_UI_PANEL_HEIGHT;

    /* Compression + partial refresh requires DPU-aligned frame geometry. */
    if (cfg.output_compress && cfg.render_mode == RENDER_PARTIAL_MODE)
    {
        if (DB_UI_PANEL_WIDTH % 16 || DB_UI_PANEL_HEIGHT % 4)
        {
            cfg.disp_width  = (DB_UI_PANEL_WIDTH + 15) & ~15;
            cfg.disp_height = (DB_UI_PANEL_HEIGHT + 3) & ~3;
        }
    }

    uint32_t fb_size = cfg.output_compress
                           ? (cfg.disp_width * cfg.disp_height)
                           : (cfg.disp_width * cfg.disp_height * sizeof(bk_color_t));

    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++)
    {
        s_frame_buffer[i] = bk_frame_buffer_malloc(
            (i % 2) ? MEM_SLAB_HEAP_UNCODED : MEM_SLAB_HEAP_CODED, fb_size);
        cfg.frame_buffer[i] = s_frame_buffer[i];
        if (s_frame_buffer[i] == NULL)
        {
            LOGE("frame buffer %d alloc failed (size=%u)\r\n", i, (unsigned)fb_size);
            doorbell_ui_free_frame_buffers();
            return BK_ERR_NO_MEM;
        }
    }

    cfg.args = s_lcd_handle;
    cfg.flush_cb = doorbell_ui_flush_cb;

    if (lv_vendor_init(&cfg) != BK_OK)
    {
        LOGE("lv_vendor_init failed\r\n");
        doorbell_ui_free_frame_buffers();
        return BK_FAIL;
    }

    /* Build the pages first (beken_ui_init internally creates and loads home), then
     * start the LVGL task. */
    lv_vendor_disp_lock();
    beken_ui_init();
    lv_vendor_disp_unlock();

    lv_vendor_start();

    s_lvgl_ready = true;
    LOGI("LVGL ready on %dx%d, home loaded\r\n", DB_UI_PANEL_WIDTH, DB_UI_PANEL_HEIGHT);
    return BK_OK;
}

/* -------------------------------------------------------------------------- */
/* Page navigation / dynamic data                                            */
/* -------------------------------------------------------------------------- */

/* Must be called while holding the disp lock: writes only if both obj and text are valid. */
static void db_ui_set_label_locked(lv_obj_t *obj, const char *text)
{
    if (obj && text)
    {
        lv_label_set_text(obj, text);
    }
}

/* Must be called while holding the disp lock: fill the provisioning device-info labels
 * with the device's real values. Name/MAC are derived from the BT MAC; Name matches
 * doorbell_netcfg's advertised name ("doorbell_XXYYZZ", using the first 3 MAC bytes). */
static void db_ui_fill_device_info_locked(void)
{
    uint8_t mac[6] = {0};
    char name[24];
    char mac_str[24];

    bk_bluetooth_get_address(mac);

    snprintf(name, sizeof(name), "doorbell_%02X%02X%02X", mac[0], mac[1], mac[2]);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    db_ui_set_label_locked(bk_lv_tool_ui.provisioning_v_name, name);
    db_ui_set_label_locked(bk_lv_tool_ui.provisioning_v_mac, mac_str);
    db_ui_set_label_locked(bk_lv_tool_ui.provisioning_v_model, DB_UI_DEVICE_MODEL);
    db_ui_set_label_locked(bk_lv_tool_ui.provisioning_v_proto, DB_UI_DEVICE_PROTO);
}

void doorbell_ui_goto(doorbell_ui_page_t page)
{
    if (!s_lvgl_ready)
    {
        return;
    }

    lv_vendor_disp_lock();
    switch (page)
    {
        case DB_UI_PAGE_HOME:
            navigate_to_screen(&bk_lv_tool_ui.home, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                               init_page_home);
            break;
        case DB_UI_PAGE_PROVISIONING:
            navigate_to_screen(&bk_lv_tool_ui.provisioning, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                               init_page_provisioning);
            /* Pages are created lazily; after navigate the labels exist, so override
             * the generated placeholder text with real values. */
            db_ui_fill_device_info_locked();
            break;
        case DB_UI_PAGE_SETTINGS:
            navigate_to_screen(&bk_lv_tool_ui.settings, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                               init_page_settings);
            break;
        default:
            break;
    }
    lv_vendor_disp_unlock();
}

void doorbell_ui_request_provisioning(void)
{
    /* 1) Clear the saved provisioning info first: erase the reconnect credentials in
     *    NV and stop the current STA, returning the device to the "unprovisioned"
     *    state so the old network does not auto-reconnect in the background and
     *    interfere with this provisioning. */
    doorbell_netcfg_erase();

    /* 2) Switch to the provisioning page immediately and show a waiting hint (do not
     *    wait for the BLE RUNNING callback, so the key feels more responsive). Pages
     *    are created lazily; after goto the device-info labels are already filled by
     *    doorbell_ui_goto. */
    doorbell_ui_goto(DB_UI_PAGE_PROVISIONING);
    doorbell_ui_set_status("Waiting for phone...");

    /* 3) Start BLE provisioning to wait for the phone to send new credentials. The
     *    RUNNING state callback (doorbell_ui_on_netcfg_event) re-confirms the page
     *    and status. */
    doorbell_netcfg_start_provisioning();
}

void doorbell_ui_set_qr(const char *payload)
{
    if (payload == NULL || !s_lvgl_ready)
    {
        return;
    }

    lv_vendor_disp_lock();
    if (bk_lv_tool_ui.provisioning_qr_code)
    {
        lv_qrcode_update(bk_lv_tool_ui.provisioning_qr_code, payload, strlen(payload));
    }
    lv_vendor_disp_unlock();
}

void doorbell_ui_set_status(const char *text)
{
    if (text == NULL || !s_lvgl_ready)
    {
        return;
    }

    /* The redesigned provisioning page dropped the dedicated status pill/label;
     * reuse the QR subtitle line (default "Bluetooth (BLE) Provisioning") to show
     * transient status such as "Waiting for phone...". */
    lv_vendor_disp_lock();
    if (bk_lv_tool_ui.provisioning_qr_subtitle)
    {
        lv_label_set_text(bk_lv_tool_ui.provisioning_qr_subtitle, text);
    }
    lv_vendor_disp_unlock();
}

void doorbell_ui_set_device_info(const char *name, const char *mac,
                                 const char *model, const char *proto)
{
    if (!s_lvgl_ready)
    {
        return;
    }

    lv_vendor_disp_lock();
    db_ui_set_label_locked(bk_lv_tool_ui.provisioning_v_name, name);
    db_ui_set_label_locked(bk_lv_tool_ui.provisioning_v_mac, mac);
    db_ui_set_label_locked(bk_lv_tool_ui.provisioning_v_model, model);
    db_ui_set_label_locked(bk_lv_tool_ui.provisioning_v_proto, proto);
    lv_vendor_disp_unlock();
}

bool doorbell_ui_is_ready(void)
{
    return s_lvgl_ready;
}

/* -------------------------------------------------------------------------- */
/* Video-call handoff (LVGL <-> two-way video pipeline share the one panel)   */
/* -------------------------------------------------------------------------- */

void doorbell_ui_set_call_active(bool active)
{
    if (active)
    {
        if (s_call_active)
        {
            return;
        }
        s_call_active = true;

        /* A call arrives before the boot media finishes: LVGL is not up yet, no
         * teardown needed, just record the intent. The boot-media-done wrap-up will,
         * based on s_call_active, tear LVGL down immediately after it comes up. */
        if (!s_lvgl_ready)
        {
            return;
        }

        /* Fully tear down LVGL, freeing the HSRAM it held (draw buffers/GPU/frame
         * buffers); otherwise the video-call pipeline's compositor and ISP cannot
         * allocate buffers from the same HSRAM pool. The panel is not turned off:
         * app_display's refcount is still held by the boot media, so the screen stays
         * lit and never blanks. */
        doorbell_ui_teardown_lvgl();
        LOGI("call active: LVGL torn down, HSRAM released to video pipeline\r\n");
    }
    else
    {
        if (!s_call_active)
        {
            return;
        }
        s_call_active = false;

        /* Key: the downlink image compositor shares the same vg_lite GPU instance as
         * LVGL, but it is not managed by mm_service (camera/audio/lcd) voting and does
         * not stop automatically when the call ends (doorbell_downlink_video_stop is
         * only called on videoIntercom reconfiguration/turnOff). If not stopped first, the
         * rebuilt LVGL would share the GPU with the still-running compositor; on the
         * next call teardown, lv_gpu_deinit->vg_lite_close would destroy the GPU the
         * compositor is using, and its GPU task would then MemFault dereferencing a
         * null pointer in set_render_target/memcmp. So stop the downlink before
         * rebuilding LVGL, letting the compositor release the GPU and HSRAM so LVGL
         * owns the GPU exclusively. If the phone keeps streaming, the next
         * videoIntercom.turnOn will bring the downlink back up. */
        if (doorbell_downlink_video_is_running())
        {
            doorbell_downlink_video_stop();
        }

        /* Call ended (the pipeline has freed its HSRAM); rebuild LVGL: re-allocate
         * frame buffers, lv_vendor_init (incl. lv_init), beken_ui_init to rebuild home,
         * and start the rendering task. start_lvgl internally restores the DPU pixel
         * format first, avoiding a glitch from carrying over the video format. */
        if (doorbell_ui_start_lvgl() != BK_OK)
        {
            LOGE("rebuild LVGL after call failed, ui not available\r\n");
            return;
        }

        /* Explicitly switch to home to cover the last video frame. */
        doorbell_ui_goto(DB_UI_PAGE_HOME);
        LOGI("call ended: LVGL rebuilt, home shown\r\n");
    }
}

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
/* multimedia service state idle<->active edge callback (triggered by
 * doorbell_mm_service_vote, runs on the network/RPC thread): entering the call view
 * when any of voice/image/display opens, and returning to home when all are closed. */
static void doorbell_ui_on_mm_status(bool active, void *user)
{
    (void)user;
    doorbell_ui_set_call_active(active);
}
#endif

/* -------------------------------------------------------------------------- */
/* Provisioning event handling (from doorbell_netcfg, on SDK thread)         */
/* -------------------------------------------------------------------------- */

static void doorbell_ui_on_netcfg_event(dbnp_ui_event_t ev, void *user)
{
    (void)user;

    switch (ev)
    {
        case DBNP_UI_PROVISIONING:
            if (s_lvgl_ready)
            {
                doorbell_ui_goto(DB_UI_PAGE_PROVISIONING);
                doorbell_ui_set_status("Waiting for phone...");
            }
            else
            {
                s_pending_prov = true;
            }
            break;

        case DBNP_UI_SUCCESS:
            /* D2: on provisioning success return to home immediately, no lingering hint. */
            if (s_lvgl_ready)
            {
                doorbell_ui_goto(DB_UI_PAGE_HOME);
            }
            else
            {
                s_pending_success = true;
            }
            break;

        case DBNP_UI_FAILED:
            if (s_lvgl_ready)
            {
                doorbell_ui_set_status("Connect failed, retrying...");
            }
            break;

        default:
            break;
    }
}

/* -------------------------------------------------------------------------- */
/* Lifecycle entry points                                                    */
/* -------------------------------------------------------------------------- */

bk_err_t doorbell_ui_init(void)
{
    /* Register the callback before provisioning starts to ensure the first
     * provisioning state is captured. LVGL is not started here; it starts after the
     * boot media finishes playing (doorbell_ui_on_boot_media_done). */
    doorbell_netcfg_set_ui_event_cb(doorbell_ui_on_netcfg_event, NULL);

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    /* Register the multimedia service state callback: exit LVGL and switch to the
     * video-call view when the phone opens voice/image/display, and switch back to
     * home when all are closed (or disconnected). See doorbell_ui_set_call_active. */
    doorbell_mm_service_set_status_cb(doorbell_ui_on_mm_status, NULL);
#endif

    LOGI("doorbell ui inited (lvgl deferred to boot-media-done)\r\n");
    return BK_OK;
}

void doorbell_ui_on_boot_media_done(bk_err_t result, void *user_data)
{
    (void)result;
    (void)user_data;

    s_boot_done = true;

    if (doorbell_ui_start_lvgl() != BK_OK)
    {
        LOGE("start lvgl failed, ui not available\r\n");
        return;
    }

    /* First-screen selection: SUCCESS already arrived or already provisioned -> home;
     * otherwise -> provisioning. */
    bool want_prov = !s_pending_success &&
                     (s_pending_prov || !doorbell_netcfg_has_saved_network());

    if (want_prov)
    {
        doorbell_ui_goto(DB_UI_PAGE_PROVISIONING);
        doorbell_ui_set_status("Waiting for phone...");
        LOGI("first screen: provisioning\r\n");
    }
    else
    {
        /* beken_ui_init already loads home by default; ensure it explicitly here. */
        doorbell_ui_goto(DB_UI_PAGE_HOME);
        LOGI("first screen: home\r\n");
    }

    s_pending_prov = false;
    s_pending_success = false;

    /* The phone started a call while the boot media was still playing: s_call_active
     * was set by set_call_active in the !s_lvgl_ready branch. Tear LVGL down fully as
     * soon as it comes up, yielding the HSRAM to the video pipeline. */
    if (s_call_active)
    {
        doorbell_ui_teardown_lvgl();
        LOGI("boot done during call: LVGL torn down immediately\r\n");
    }
}
