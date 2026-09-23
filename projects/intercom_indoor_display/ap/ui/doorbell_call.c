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
 * intercom_indoor_display local call controller (front-end of CONFIG_DOOR_ACCESS_CALL_FLOW).
 *
 * Ties the smart_intercom call-control core (doorbell_call_ctrl.h) to the local
 * UX:
 *
 *   App videoIntercom.turnOn  --INCOMING-->  worker: show the designed来电 (incoming
 *                                            _call) LVGL screen, start local ringtone,
 *                                            LVGL stays up.
 *   user taps Answer / K3     --request_answer--> worker: stop ringtone, tear LVGL
 *                                            down (yield HSRAM), doorbell_call_answer()
 *                                            starts two-way media; on ACTIVE the on-video
 *                                            hang-up + lock OSD buttons are blitted.
 *   user taps OSD hang-up / Decline / K6 / remote hangup / turnOff --ENDED--> worker:
 *                                            hide OSD, stop media, rebuild LVGL and
 *                                            return home.
 *   user taps OSD lock        --toggle door state, swap closed/open lock sprite.
 *
 * Threading: the LVGL button callbacks, the physical-key handler and the
 * smart_intercom event/mm-status callbacks only *enqueue* an action; every heavy
 * operation (LVGL teardown/rebuild, media start/stop, GPU OSD) runs on this
 * module's own worker thread. A separate low-rate touch thread hit-tests the OSD
 * hang-up and lock buttons while LVGL is torn down during an active call (LVGL
 * owns touch itself during ringing, so the touch thread must not steal events then).
 */

#include <common/bk_include.h>
#include <common/bk_err.h>
#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <string.h>

#include "doorbell_call.h"

#define TAG "db-call"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#if CONFIG_DOOR_ACCESS_CALL_FLOW

#include "lvgl.h"
#include "lv_vendor.h"
#if CONFIG_TP
#include "driver/drv_tp.h"
#endif

#include "beken_ui.h"
#include "event_runtime.h"
#include "doorbell_ui.h"
#include "doorbell_ui_i18n.h"
#include "doorbell_ringtone.h"
#include "doorbell_call_ctrl.h"          /* smart_intercom call-control core */
#include "doorbell_display_compositor.h" /* OSD sprite blit                  */
#include "doorbell_cmd.h"                /* multimedia service status cb     */
#include "osd_call_timer.h"

#include <driver/timer.h>

/* -------------------------------------------------------------------------- */
/* Geometry (all in the panel's final portrait output space, 720x1280).       */
/* The compositor reports its output post-rotation as 720x1280 and the OSD is  */
/* submitted with rotation_degree=0 in that space; the touch panel is opened   */
/* at the same 720x1280 so hit-test coordinates line up 1:1.                   */
/* NOTE: verify TP orientation/mirror against the indoor-display board before   */
/* flashing (see CONFIG_TP_* in defconfig).                                    */
/* -------------------------------------------------------------------------- */
#define PANEL_W            (720)
#define PANEL_H            (1280)

#define OSD_SIZE           (144)                              /* 144 canvas, 140 button + 2px pad */
#define OSD_GAP            (72)                               /* gap between the two 144 canvases */
/* In-call pair is centered: unlock on the left, hang-up on the right.
 * 144+72+144 = 360, origin (720-360)/2 = 180. Sit lower than the incoming
 * row (990) — no caption under the in-call buttons, closer to WeChat. */
#define OSD_PAIR_W         (OSD_SIZE + OSD_GAP + OSD_SIZE)    /* 360 */
#define OSD_PAIR_X         ((PANEL_W - OSD_PAIR_W) / 2)       /* 180 */
#define OSD_LOCK_X         (OSD_PAIR_X)                       /* 180, left  */
#define OSD_HANGUP_X       (OSD_PAIR_X + OSD_SIZE + OSD_GAP)  /* 396, right */
#define OSD_DST_Y          (1060)                             /* incoming 988 + 72 */
#define OSD_TIME_X         ((PANEL_W - OSD_CALL_TIMER_W) / 2) /* 232, top-center */
#define OSD_TIME_Y         (80)
#define OSD_LOCK_DEBOUNCE_MS (400)

/* -------------------------------------------------------------------------- */
/* Worker action queue                                                        */
/* -------------------------------------------------------------------------- */
typedef enum
{
    ACT_INCOMING = 0, /* invite from App (smart_intercom INCOMING event)   */
    ACT_ACTIVE,       /* media up (smart_intercom ACTIVE event)            */
    ACT_ENDED,        /* call ended (event / mm-idle / remote)             */
    ACT_ANSWER,       /* local answer request (touch / key)               */
    ACT_HANGUP,       /* local hangup / decline request (touch / key/OSD) */
    ACT_LOCK_TOGGLE,  /* in-call OSD lock button (same state as 来电 lock) */
} call_action_t;

/* Local UX phase. Distinct from doorbell_call_state_t: PH_ANSWERING covers the
 * window after LVGL teardown but before media reports ACTIVE, so a stray event
 * cannot re-trigger answer or mishandle the return path. */
typedef enum
{
    PH_HOME = 0,   /* no call; normal UI                                   */
    PH_RINGING,    /* incoming screen shown, LVGL up, waiting for answer   */
    PH_ANSWERING,  /* answer accepted, LVGL torn down, media coming up     */
    PH_CALL,       /* two-way media running, OSD hang-up + lock shown      */
} ui_phase_t;

static volatile ui_phase_t s_phase = PH_HOME;
static bool                s_inited;

static beken_queue_t       s_q;         /* call_action_t items                   */
static beken_thread_t      s_worker;
static volatile bool       s_run = true;
#if CONFIG_TP
static beken_thread_t      s_touch;
static beken_semaphore_t   s_touch_sem; /* signalled by the TP notify callback   */
#endif

static volatile uint32_t   s_call_elapsed_sec;
static bool                s_call_hw_timer;
static uint32_t            s_call_t0_ms;     /* fallback if HW timer start fails */
static uint32_t            s_call_shown_sec;

#define WORKER_STACK   (6 * 1024)
#define TOUCH_STACK    (3 * 1024)
#define WORKER_PRIO    (5)
#define TOUCH_PRIO     (6)
#define Q_DEPTH        (8)

static void call_post(call_action_t act)
{
    if (s_q == NULL)
    {
        return;
    }
    uint8_t v = (uint8_t)act;
    if (rtos_push_to_queue(&s_q, &v, BEKEN_NO_WAIT) != BK_OK)
    {
        LOGW("action queue full, drop act=%d\n", (int)act);
    }
}

/* -------------------------------------------------------------------------- */
/* In-call OSD: hang-up + lock (pre-rendered ARGB8888, same look as 来电)      */
/*                                                                            */
/* Hang-up: 140x140 #EF4444 circle + decline_call2 96x96, 144 canvas.          */
/* Lock:    140x140 #000000 circle + closed/open 72x72, centroid-centered.     */
/* uint32 layout is (A<<24)|(R<<16)|(G<<8)|B == B,G,R,A on little-endian ==    */
/* VG_LITE_BGRA8888, which is what the GPU reads for BK_PIXEL_FORMAT_ARGB8888. */
/* -------------------------------------------------------------------------- */
extern const uint32_t g_hangup_icon_argb8888[OSD_SIZE * OSD_SIZE];
extern const uint32_t g_lock_closed_icon_argb8888[OSD_SIZE * OSD_SIZE];
extern const uint32_t g_lock_open_icon_argb8888[OSD_SIZE * OSD_SIZE];

/*
 * Call duration must track wall time, not FreeRTOS ticks.
 *
 * SysTick is clocked from the 32K LPO (CONFIG_SYSTICK_32K) and counted as
 * 1 ms/tick at a nominal 32000 Hz. This board uses the internal ROSC
 * (CONFIG_EXTERN_32K and CONFIG_LPO_SRC_26M32K are off). ROSC is typically
 * a few percent fast vs the 26 MHz xtal, so rtos_get_time() runs fast vs a
 * phone stopwatch. AON RTC uses the same LPO and the same 32000 Hz formula,
 * so switching to bk_aon_rtc_get_ms() would not fix it. AP does not link
 * the ROSC CKMN helper, but TIMER_ID12-17 count the AHBP bus clock derived
 * from the 26 MHz xtal PLL — that is the accurate source while the panel
 * stays awake for the call.
 */
#define CALL_DURATION_TIMER_ID  TIMER_ID17

static void call_duration_timer_isr(timer_id_t id)
{
    (void)id;
    s_call_elapsed_sec++;
}

static void call_duration_timer_stop(void)
{
    if (s_call_hw_timer)
    {
        (void)bk_timer_stop(CALL_DURATION_TIMER_ID);
        s_call_hw_timer = false;
    }
}

static void call_duration_timer_start(void)
{
    bk_err_t ret;

    call_duration_timer_stop();
    s_call_elapsed_sec = 0;
    s_call_t0_ms = rtos_get_time();
    ret = bk_timer_start(CALL_DURATION_TIMER_ID, 1000, call_duration_timer_isr);
    s_call_hw_timer = (ret == BK_OK);
    if (!s_call_hw_timer)
    {
        LOGW("call duration HW timer start failed=%d, fallback to OS tick\n", (int)ret);
    }
}

static uint32_t call_elapsed_sec(void)
{
    if (s_call_hw_timer)
    {
        return s_call_elapsed_sec;
    }
    return (rtos_get_time() - s_call_t0_ms) / 1000U;
}

static void osd_show_call_overlays(void)
{
    doorbell_osd_sprite_t spr[3];
    const uint32_t *lock_px = doorbell_ui_door_is_unlocked()
                              ? g_lock_open_icon_argb8888
                              : g_lock_closed_icon_argb8888;
    uint32_t elapsed = call_elapsed_sec();
    uint8_t n = 2U;
    bk_err_t ret;

    /* The whole set is resubmitted on every refresh, but the compositor only
     * re-composites what changed: the buttons keep the overlay layer they are
     * already blitted from while the timer swaps once a second. Names must stay
     * stable for that matching to work. */
    os_memset(spr, 0, sizeof(spr));
    spr[0].name = "lock";
    spr[0].argb8888 = lock_px;
    spr[0].width = OSD_SIZE;
    spr[0].height = OSD_SIZE;
    spr[0].dst_x = OSD_LOCK_X;
    spr[0].dst_y = OSD_DST_Y;
    spr[1].name = "hangup";
    spr[1].argb8888 = g_hangup_icon_argb8888;
    spr[1].width = OSD_SIZE;
    spr[1].height = OSD_SIZE;
    spr[1].dst_x = OSD_HANGUP_X;
    spr[1].dst_y = OSD_DST_Y;

    s_call_shown_sec = elapsed;
    if (osd_call_timer_acquire() == BK_OK)
    {
        osd_call_timer_paint(elapsed);
        spr[2].name = "time";
        spr[2].argb8888 = osd_call_timer_argb();
        spr[2].width = OSD_CALL_TIMER_W;
        spr[2].height = OSD_CALL_TIMER_H;
        spr[2].dst_x = OSD_TIME_X;
        spr[2].dst_y = OSD_TIME_Y;
        /* Repainted in place, so the pixel pointer alone cannot show it. */
        spr[2].dirty = true;
        n = 3U;
    }

    ret = doorbell_compositor_osd_show_n(spr, n, 0);
    if (ret != BK_OK)
    {
        LOGW("osd_show failed=%d\n", ret);
    }
}

static void osd_tick_if_needed(void)
{
    uint32_t elapsed;

    if (s_phase != PH_CALL)
    {
        return;
    }
    elapsed = call_elapsed_sec();
    if (elapsed != s_call_shown_sec)
    {
        osd_show_call_overlays();
    }
}

/* -------------------------------------------------------------------------- */
/* Incoming (来电) screen: navigate to the designed page and wire its buttons  */
/* -------------------------------------------------------------------------- */
static void incoming_answer_btn_cb(lv_event_t *e)
{
    (void)e;
    doorbell_call_request_answer();
}

static void incoming_decline_btn_cb(lv_event_t *e)
{
    (void)e;
    doorbell_call_request_hangup();
}

/* Incoming-call lock: Designer ships closed + open sprites on the same button.
 * Both icons are white, so the button stays black. Shares door state with Home.
 *
 * Designer places the 72x72 images at (13,21). That is already the correct
 * offset once LVGL's default button pad (pad_hor≈20, pad_ver≈11 on this
 * 1280 panel) is applied. Overwriting those coords without clearing pad
 * shifted the glyph ~20px to the right. Zero the theme pad and center the
 * canvas in the 140 circle; the lock pixels themselves sit on x≈36 in 72. */
static void call_lock_center_icons(void)
{
    lv_obj_t *btn  = bk_lv_tool_ui.incoming_call_lock_btn;
    lv_obj_t *ic   = bk_lv_tool_ui.incoming_call_lock_ic;
    lv_obj_t *open = bk_lv_tool_ui.incoming_call_lock_ic_open;

    if (btn && lv_obj_is_valid(btn))
    {
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    }
    if (ic && lv_obj_is_valid(ic))
    {
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, 0);
    }
    if (open && lv_obj_is_valid(open))
    {
        lv_obj_align(open, LV_ALIGN_CENTER, 0, 0);
    }
}

static void call_lock_apply_ui(bool unlocked)
{
    lv_obj_t *btn  = bk_lv_tool_ui.incoming_call_lock_btn;
    lv_obj_t *ic   = bk_lv_tool_ui.incoming_call_lock_ic;
    lv_obj_t *open = bk_lv_tool_ui.incoming_call_lock_ic_open;
    lv_obj_t *lbl  = bk_lv_tool_ui.incoming_call_lock_lbl;

    if (btn)
    {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A1A),
                                  LV_PART_MAIN | LV_STATE_PRESSED);
    }
    if (ic)
    {
        lv_obj_clear_flag(ic, LV_OBJ_FLAG_CLICKABLE);
        if (unlocked)
        {
            lv_obj_add_flag(ic, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_remove_flag(ic, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (open)
    {
        lv_obj_clear_flag(open, LV_OBJ_FLAG_CLICKABLE);
        if (unlocked)
        {
            lv_obj_remove_flag(open, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(open, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (lbl)
    {
        lv_label_set_text(lbl, doorbell_ui_str_lock(unlocked));
    }
}

static void call_lock_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    bool unlocked = !doorbell_ui_door_is_unlocked();
    doorbell_ui_door_set(unlocked);
    call_lock_apply_ui(unlocked);
}

static void call_lock_bind(void)
{
    if (bk_lv_tool_ui.incoming_call_lock_btn == NULL)
    {
        return;
    }
    lv_obj_remove_event_cb(bk_lv_tool_ui.incoming_call_lock_btn, call_lock_event_cb);
    lv_obj_add_event_cb(bk_lv_tool_ui.incoming_call_lock_btn, call_lock_event_cb,
                        LV_EVENT_CLICKED, NULL);
    call_lock_center_icons();
    call_lock_apply_ui(doorbell_ui_door_is_unlocked());
}

/* Designer bumped incoming-call fonts (50/40/34) but left the old label boxes
 * (h=40/28/26). Grow height after generated init so glyphs are not clipped.
 * Do not edit incoming_call_init.c. */
static void call_incoming_fix_label_heights(void)
{
    lv_obj_t *name = bk_lv_tool_ui.incoming_call_caller_name;
    lv_obj_t *st   = bk_lv_tool_ui.incoming_call_call_status;
    lv_obj_t *dec  = bk_lv_tool_ui.incoming_call_dec_lbl;
    lv_obj_t *ans  = bk_lv_tool_ui.incoming_call_ans_lbl;
    lv_obj_t *lock = bk_lv_tool_ui.incoming_call_lock_lbl;

    if (name && lv_obj_is_valid(name))
    {
        lv_obj_set_height(name, 56);
    }
    if (st && lv_obj_is_valid(st))
    {
        lv_obj_set_height(st, 48);
    }
    if (dec && lv_obj_is_valid(dec))
    {
        lv_obj_set_height(dec, 40);
    }
    if (ans && lv_obj_is_valid(ans))
    {
        lv_obj_set_height(ans, 40);
    }
    if (lock && lv_obj_is_valid(lock))
    {
        lv_obj_set_height(lock, 40);
    }
}

static void show_incoming_screen_locked_free(void)
{
    /* Must run with LVGL up. Lazily (re)creates the incoming_call page from the
     * generated init, then (re)attaches our click handlers. remove-before-add
     * keeps it idempotent when the page object is reused across rings. */
    if (!doorbell_ui_is_ready())
    {
        LOGW("LVGL not ready, cannot show incoming screen\n");
        return;
    }

    lv_vendor_disp_lock();

    navigate_to_screen(&bk_lv_tool_ui.incoming_call, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                       false, init_page_incoming_call);

    if (bk_lv_tool_ui.incoming_call_answer_btn != NULL)
    {
        lv_obj_remove_event_cb(bk_lv_tool_ui.incoming_call_answer_btn,
                               incoming_answer_btn_cb);
        lv_obj_add_event_cb(bk_lv_tool_ui.incoming_call_answer_btn,
                            incoming_answer_btn_cb, LV_EVENT_CLICKED, NULL);
    }
    if (bk_lv_tool_ui.incoming_call_decline_btn != NULL)
    {
        lv_obj_remove_event_cb(bk_lv_tool_ui.incoming_call_decline_btn,
                               incoming_decline_btn_cb);
        lv_obj_add_event_cb(bk_lv_tool_ui.incoming_call_decline_btn,
                            incoming_decline_btn_cb, LV_EVENT_CLICKED, NULL);
    }

    call_incoming_fix_label_heights();
    call_lock_bind();
    doorbell_ui_apply_lang();

    lv_vendor_disp_unlock();
    LOGI("incoming screen shown\n");
}

/* Return to the home screen at call end. If LVGL was torn down (answered path),
 * rebuild it via doorbell_ui_set_call_active(false); otherwise (declined while
 * ringing) LVGL is still up so just navigate home. */
static void return_home_after_call(void)
{
    if (!doorbell_ui_is_ready())
    {
        doorbell_ui_set_call_active(false); /* rebuild LVGL + go home */
    }
    else
    {
        doorbell_ui_goto(DB_UI_PAGE_HOME);
    }
}

/* -------------------------------------------------------------------------- */
/* Worker thread: serializes all heavy transitions                            */
/* -------------------------------------------------------------------------- */
static void handle_incoming(void)
{
    if (s_phase != PH_HOME)
    {
        LOGW("INCOMING ignored, phase=%d\n", (int)s_phase);
        return;
    }
    show_incoming_screen_locked_free();
    s_phase = PH_RINGING;
    if (doorbell_ringtone_start() != BK_OK)
    {
        LOGW("incoming ringtone failed to start\n");
    }
}

static void handle_answer(void)
{
    if (s_phase != PH_RINGING)
    {
        LOGW("ANSWER ignored, phase=%d\n", (int)s_phase);
        return;
    }

    /* Stop the ringtone before taking the DAC for the call voice path. */
    s_phase = PH_ANSWERING;
    doorbell_ringtone_stop();

    /* Yield the panel/HSRAM to the video pipeline BEFORE media start: tears LVGL
     * down (incoming screen disappears, panel frozen on last UI frame). */
    doorbell_ui_set_call_active(true);

    /* Start two-way media using the invite's stashed params. On success the core
     * fires ACTIVE (-> ACT_ACTIVE -> OSD); on failure it fires ENDED. */
    bk_err_t ret = doorbell_call_answer();
    if (ret != BK_OK)
    {
        LOGE("answer failed=%d\n", ret);
        /* core already fired ENDED on the failure path; nothing else to do. */
    }
}

static void handle_active(void)
{
    if (s_phase != PH_ANSWERING && s_phase != PH_RINGING)
    {
        LOGW("ACTIVE ignored, phase=%d\n", (int)s_phase);
        return;
    }
    s_phase = PH_CALL;
    doorbell_ringtone_stop();
    call_duration_timer_start();
    osd_show_call_overlays();
    LOGI("call active: full-screen remote + hang-up/lock/timer OSD (hw_timer=%d)\n",
         (int)s_call_hw_timer);
}

static void handle_ended(void)
{
    if (s_phase == PH_HOME)
    {
        return; /* dedup: ENDED can arrive from both the event cb and mm-idle */
    }

    ui_phase_t prev = s_phase;
    s_phase = PH_HOME;
    doorbell_ringtone_stop();

    /* Hide the OSD while the compositor is (usually) still alive. Safe no-op if
     * it was never shown or the compositor already stopped. */
    if (prev == PH_CALL)
    {
        call_duration_timer_stop();
        doorbell_compositor_osd_hide();
        osd_call_timer_release();
    }

    /* Ensure the core stops media / releases state for a remote or mm-driven end
     * (idempotent: a no-op once already idle). */
    doorbell_call_hangup();

    return_home_after_call();
    LOGI("call ended, back home (prev phase=%d)\n", (int)prev);
}

static void handle_hangup(void)
{
    if (s_phase == PH_HOME)
    {
        return;
    }

    /* Local hangup/decline: stop ringtone / hide OSD first, then ask the core
     * to stop; the core fires ENDED which finishes the return-home in
     * handle_ended(). */
    doorbell_ringtone_stop();
    if (s_phase == PH_CALL)
    {
        call_duration_timer_stop();
        doorbell_compositor_osd_hide();
        osd_call_timer_release();
    }
    doorbell_call_hangup();
}

static void handle_lock_toggle(void)
{
    static uint32_t s_last_ms;
    uint32_t now;

    if (s_phase != PH_CALL)
    {
        return;
    }

    now = rtos_get_time();
    if ((now - s_last_ms) < OSD_LOCK_DEBOUNCE_MS)
    {
        return;
    }
    s_last_ms = now;

    doorbell_ui_door_set(!doorbell_ui_door_is_unlocked());
    osd_show_call_overlays();
}

static void worker_thread(void *arg)
{
    (void)arg;

    while (s_run)
    {
        uint8_t act;
        uint32_t wait = (s_phase == PH_CALL) ? 250U : BEKEN_WAIT_FOREVER;

        if (rtos_pop_from_queue(&s_q, &act, wait) != BK_OK)
        {
            osd_tick_if_needed();
            continue;
        }

        switch ((call_action_t)act)
        {
            case ACT_INCOMING: handle_incoming(); break;
            case ACT_ACTIVE:   handle_active();   break;
            case ACT_ENDED:    handle_ended();    break;
            case ACT_ANSWER:      handle_answer();      break;
            case ACT_HANGUP:      handle_hangup();      break;
            case ACT_LOCK_TOGGLE: handle_lock_toggle(); break;
            default: break;
        }
    }

    rtos_delete_thread(NULL);
}

/* -------------------------------------------------------------------------- */
/* Touch thread: OSD hang-up / lock hit-test while LVGL is torn down           */
/*                                                                            */
/* The touch panel itself is opened once by doorbell_ui (doorbell_ui_start_    */
/* lvgl -> drv_tp_open, kept open across the LVGL teardown/rebuild), matching   */
/* the video_intercom reference. During ringing LVGL owns touch via its        */
/* pointer indev (lv_port_indev); this thread only reads drv_tp directly once   */
/* LVGL is torn down for the active call, to hit-test the on-video OSD buttons. */
/* -------------------------------------------------------------------------- */
#if CONFIG_TP
static bool point_in_rect(uint16_t x, uint16_t y, uint16_t ox, uint16_t oy)
{
    return (x >= ox && x < (ox + OSD_SIZE) &&
            y >= oy && y < (oy + OSD_SIZE));
}

static void touch_thread(void *arg)
{
    (void)arg;

    while (s_run)
    {
        /* Wake on a touch notify or every 100ms as a backup. */
        (void)rtos_get_semaphore(&s_touch_sem, 100);

        /* Only consume touch events while LVGL is down (active call). During
         * ringing LVGL owns the panel and polls drv_tp itself; draining here
         * would steal the Answer/Decline button presses. */
        if (s_phase != PH_CALL)
        {
            continue;
        }

        tp_point_infor_t pt;
        while (drv_tp_read(&pt) == 0 /* kNoErr */)
        {
            if (pt.m_state == 1 /* press */)
            {
                if (point_in_rect(pt.m_x, pt.m_y, OSD_HANGUP_X, OSD_DST_Y))
                {
                    doorbell_call_request_hangup();
                    break;
                }
                if (point_in_rect(pt.m_x, pt.m_y, OSD_LOCK_X, OSD_DST_Y))
                {
                    call_post(ACT_LOCK_TOGGLE);
                    break;
                }
            }
            if (!pt.m_need_continue)
            {
                break;
            }
        }
    }

    rtos_delete_thread(NULL);
}

static void tp_notify_cb(void *arg)
{
    (void)arg;
    if (s_touch_sem != NULL)
    {
        rtos_set_semaphore(&s_touch_sem);
    }
}
#endif /* CONFIG_TP */

/* -------------------------------------------------------------------------- */
/* smart_intercom callbacks (light: enqueue only)                             */
/* -------------------------------------------------------------------------- */
static void on_call_event(doorbell_call_event_t evt, void *user)
{
    (void)user;
    switch (evt)
    {
        case DOORBELL_CALL_EVT_INCOMING: call_post(ACT_INCOMING); break;
        case DOORBELL_CALL_EVT_ACTIVE:   call_post(ACT_ACTIVE);   break;
        case DOORBELL_CALL_EVT_ENDED:    call_post(ACT_ENDED);    break;
        default: break;
    }
}

/* Multimedia service idle<->active edge (from doorbell_mm_service_vote). Used
 * only as a backup end-of-call trigger (e.g. peer stream dies without a clean
 * turnOff): on the active->idle edge, enqueue ENDED. The active edge is ignored
 * because the answer path already drives the call-active handoff explicitly. */
static void on_mm_status(bool active, void *user)
{
    (void)user;
    if (!active)
    {
        call_post(ACT_ENDED);
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
void doorbell_call_request_answer(void)
{
    call_post(ACT_ANSWER);
}

void doorbell_call_request_hangup(void)
{
    call_post(ACT_HANGUP);
}

bool doorbell_call_is_busy(void)
{
    return s_phase != PH_HOME;
}

bk_err_t doorbell_call_ui_init(void)
{
    if (s_inited)
    {
        return BK_OK;
    }

    if (rtos_init_queue(&s_q, "db_call_q", sizeof(uint8_t), Q_DEPTH) != BK_OK)
    {
        LOGE("action queue init failed\n");
        return BK_FAIL;
    }

    s_run = true;

    if (rtos_create_thread(&s_worker, WORKER_PRIO, "db_call",
                           (beken_thread_function_t)worker_thread,
                           WORKER_STACK, NULL) != BK_OK)
    {
        LOGE("worker thread create failed\n");
        return BK_FAIL;
    }

#if CONFIG_TP
    /* The touch panel is opened by doorbell_ui (doorbell_ui_start_lvgl ->
     * drv_tp_open, once, kept open across the call teardown/rebuild), matching
     * the video_intercom reference. Here we only subscribe to the TP notify and
     * spin up the OSD hit-test thread; both read the already-open driver. Its
     * drv_tp_read before open (or if TP is unavailable) simply returns "no data",
     * so ordering vs. LVGL bring-up is safe. */
    if (rtos_init_semaphore(&s_touch_sem, 1) != BK_OK)
    {
        LOGE("touch sem init failed\n");
        return BK_FAIL;
    }
    drv_tp_reg_touch_event(tp_notify_cb, NULL);
    if (rtos_create_thread(&s_touch, TOUCH_PRIO, "db_call_tp",
                           (beken_thread_function_t)touch_thread,
                           TOUCH_STACK, NULL) != BK_OK)
    {
        LOGE("touch thread create failed\n");
        return BK_FAIL;
    }
#endif

    /* Register with the call-control core LAST, so no event can arrive before
     * the worker/queue are up. Overrides doorbell_ui's own mm-status handler
     * (last registration wins), giving this module sole ownership of the
     * LVGL<->video handoff in the indoor-display call flow. */
    doorbell_call_set_event_cb(on_call_event, NULL);
    doorbell_mm_service_set_status_cb(on_mm_status, NULL);

    s_inited = true;
    return BK_OK;
}

#else /* !CONFIG_DOOR_ACCESS_CALL_FLOW: stubs so callers link unconditionally */

bk_err_t doorbell_call_ui_init(void)      { return BK_OK; }
void     doorbell_call_request_answer(void) { }
void     doorbell_call_request_hangup(void) { }
bool     doorbell_call_is_busy(void)      { return false; }

#endif /* CONFIG_DOOR_ACCESS_CALL_FLOW */
