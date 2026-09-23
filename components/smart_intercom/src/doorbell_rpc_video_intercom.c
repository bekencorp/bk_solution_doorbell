#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>

#include "cJSON.h"

#include "doorbell_rpc_internal.h"
#include "doorbell_downlink_video.h"
#include "doorbell_devices_intercom.h"
#if CONFIG_DOOR_ACCESS_CALL_FLOW
#include "doorbell_call_ctrl.h"
#include "doorbell_jsonrpc.h"
#endif

#define TAG "db-rpc-vi"
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Device working mode (see db_work_mode_t). Single writer: the RPC dispatch
 * runs the handlers serially on the control-channel thread, so a plain static
 * is sufficient. */
static db_work_mode_t s_work_mode = DB_WORK_MODE_IDLE;

db_work_mode_t doorbell_rpc_work_mode_get(void)
{
    return s_work_mode;
}

void doorbell_rpc_work_mode_set(db_work_mode_t mode)
{
    s_work_mode = mode;
}

static cJSON *db_json_int(cJSON *obj, const char *key)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, key) : NULL;
    return (it != NULL && cJSON_IsNumber(it)) ? it : NULL;
}

/* Parse the "downlink" sub-object (fields identical to
 * doorbell.imageStream.setReceiveConfig params: imageFormat + formatConfig)
 * into a downlink H.264 receive config. Only h264 downlink is supported. */
static bk_err_t vi_parse_downlink(cJSON *downlink, doorbell_downlink_h264_config_t *out)
{
    cJSON *image_format = downlink ? cJSON_GetObjectItem(downlink, "imageFormat") : NULL;
    cJSON *format_config = downlink ? cJSON_GetObjectItem(downlink, "formatConfig") : NULL;
    cJSON *h264;
    cJSON *w;
    cJSON *h;
    cJSON *fps;
    cJSON *pfc;

    if (image_format == NULL || !cJSON_IsString(image_format) || format_config == NULL)
    {
        return BK_FAIL;
    }

    if (os_strcmp(image_format->valuestring, "h264") != 0)
    {
        return BK_FAIL;
    }

    h264 = cJSON_GetObjectItem(format_config, "h264");
    w = db_json_int(h264, "width");
    h = db_json_int(h264, "height");
    fps = db_json_int(h264, "fps");
    pfc = db_json_int(h264, "pFrameCount");

    if (w == NULL || h == NULL)
    {
        return BK_FAIL;
    }

    os_memset(out, 0, sizeof(*out));
    out->width = (uint16_t)w->valueint;
    out->height = (uint16_t)h->valueint;
    out->fps = (fps != NULL) ? (uint16_t)fps->valueint : 0;
    out->p_frame_count = (pfc != NULL) ? (uint16_t)pfc->valueint : 0;
    return BK_OK;
}

/* Build a {"reason": <msg>} data object for -32003 error replies. */
static cJSON *vi_reason(const char *reason)
{
    cJSON *data = cJSON_CreateObject();
    if (data != NULL)
    {
        cJSON_AddStringToObject(data, "reason", (reason != NULL) ? reason : "");
    }
    return data;
}

/*
 * Start both directions of the two-way video intercom.
 *
 *   - uplink   : camera capture + H.264 encode + video transfer to the App
 *                (@p uplink, fields identical to doorbell.camera.turnOn)
 *   - downlink : receive + decode + display the App image stream (@p dl_cfg,
 *                already parsed from the "downlink" object)
 *
 * Atomicity: the uplink encoder is opened first so it can allocate from
 * MEM_SLAB_HEAP_CODED before the downlink H.264 slot pool; if the downlink then
 * fails, the uplink is rolled back. On failure *err_code / *err_msg are filled
 * for the RPC error reply and *dl_failed marks a downlink-stage failure (so the
 * caller can attach the {"reason":"downlink"} data field). Returns BK_OK on
 * success (s_work_mode advanced to DB_WORK_MODE_INTERCOM).
 */
/*
 * Whether an answered two-way call also opens the local camera uplink.
 *
 * Indoor-display call flow (CONFIG_DOOR_ACCESS_CALL_FLOW): controlled by
 * CONFIG_DOOR_ACCESS_CALL_LOCAL_UPLINK, default off, so the call is
 * downlink-only (the App image is shown, nothing local is captured/encoded/sent).
 * Stock video_intercom always uplinks.
 */
#if CONFIG_DOOR_ACCESS_CALL_FLOW && !CONFIG_DOOR_ACCESS_CALL_LOCAL_UPLINK
#define VI_LOCAL_UPLINK_ENABLED  (0)
#else
#define VI_LOCAL_UPLINK_ENABLED  (1)
#endif

static bk_err_t vi_media_start(cJSON *uplink, const doorbell_downlink_h264_config_t *dl_cfg,
                               int *err_code, const char **err_msg, bool *dl_failed)
{
    const bool uplink_enabled = (VI_LOCAL_UPLINK_ENABLED != 0);
    int up_err_code = DB_RPC_ERR_INTERNAL;
    const char *up_err_msg = "uplink open failed";

    (void)uplink;

    if (dl_failed != NULL)
    {
        *dl_failed = false;
    }

    if (s_work_mode == DB_WORK_MODE_SINGLE)
    {
        /* Single-direction image transfer owns the uplink; require an explicit
         * camera.turnOff before switching into the two-way intercom. */
        if (err_code != NULL) *err_code = DB_RPC_ERR_NOT_SUPPORT;
        if (err_msg != NULL) *err_msg = "single-direction camera active, turn off first";
        return BK_FAIL;
    }

    if (s_work_mode == DB_WORK_MODE_INTERCOM)
    {
        /* Reconfigure: tear the current session down so the new parameters take
         * effect cleanly (camera.turnOn is a no-op when already open). */
        (void)doorbell_rpc_camera_uplink_close();
        (void)doorbell_downlink_video_stop();
        s_work_mode = DB_WORK_MODE_IDLE;
    }

    /* Uplink first: H.264 encoder vcenc_h264_memalloc() needs MEM_SLAB_HEAP_CODED
     * (coeff/ref buffers). Downlink img_manager pre-allocates coded slots and must
     * not run first or vcenc returns VCENC_EWL_MEMORY_ERROR (-6). Skip the
     * single-view preview GPU; downlink compositor owns GPU with HSRAM budgeting.
     *
     * When the local uplink is disabled the call is downlink-only: no camera is
     * opened, so the preview-GPU hold and the concurrent-uplink downlink budget
     * are both left off (the downlink gets the full memory budget). */
    if (uplink_enabled)
    {
        doorbell_devices_preview_gpu_hold(true);
        doorbell_downlink_set_concurrent_uplink(true);

        if (doorbell_rpc_camera_uplink_open_from_params(uplink, &up_err_code, &up_err_msg) != BK_OK)
        {
            doorbell_downlink_set_concurrent_uplink(false);
            doorbell_devices_preview_gpu_hold(false);
            LOGE("intercom media start: uplink open failed (%s)\n", up_err_msg ? up_err_msg : "");
            if (err_code != NULL) *err_code = up_err_code;
            if (err_msg != NULL) *err_msg = up_err_msg;
            return BK_FAIL;
        }
    }

    if (doorbell_downlink_set_h264_receive_config(dl_cfg) != BK_OK)
    {
        if (uplink_enabled)
        {
            doorbell_downlink_set_concurrent_uplink(false);
            (void)doorbell_rpc_camera_uplink_close();
            doorbell_devices_preview_gpu_hold(false);
        }
        LOGE("intercom media start: downlink open failed%s\n",
             uplink_enabled ? ", rolling back uplink" : "");
        if (err_code != NULL) *err_code = DB_RPC_ERR_NOT_SUPPORT;
        if (err_msg != NULL) *err_msg = "downlink open failed";
        if (dl_failed != NULL) *dl_failed = true;
        return BK_FAIL;
    }

    if (uplink_enabled)
    {
        doorbell_downlink_set_concurrent_uplink(false);
        doorbell_devices_preview_gpu_hold(false);
    }

    s_work_mode = DB_WORK_MODE_INTERCOM;
    LOGI("intercom media start: %s downlink %ux%u up\n",
         uplink_enabled ? "uplink +" : "(no uplink)",
         (unsigned)dl_cfg->width, (unsigned)dl_cfg->height);
    return BK_OK;
}

/* Stop both directions. Uplink is closed before the downlink so any local
 * overlay (self-view PIP) drops while its ISP SP source is still valid, then the
 * downlink decode/display pipeline is torn down. Safe in any mode. */
static void vi_media_stop(void)
{
    (void)doorbell_rpc_camera_uplink_close();
    (void)doorbell_downlink_video_stop();
    s_work_mode = DB_WORK_MODE_IDLE;
}

#if !CONFIG_DOOR_ACCESS_CALL_FLOW
/* ===================== Stock video_intercom behavior ===================== */

/*
 * doorbell.videoIntercom.turnOn - one command opens both directions of the
 * two-way video intercom (uplink capture/encode/send + downlink receive/decode/
 * display). Idempotent: a repeated call while already in intercom mode
 * reconfigures with the new parameters (tear down first, then re-open).
 */
bk_err_t doorbell_rpc_video_intercom_turn_on(cJSON *params, cJSON *id)
{
    cJSON *uplink = params ? cJSON_GetObjectItem(params, "uplink") : NULL;
    cJSON *downlink = params ? cJSON_GetObjectItem(params, "downlink") : NULL;
    doorbell_downlink_h264_config_t dl_cfg;
    int err_code = DB_RPC_ERR_INTERNAL;
    const char *err_msg = "intercom start failed";
    bool dl_failed = false;

    if (uplink == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Missing uplink", NULL);
    }
    if (downlink == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Missing downlink", NULL);
    }

    /* Validate the downlink config before touching any hardware. */
    if (vi_parse_downlink(downlink, &dl_cfg) != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid downlink config", NULL);
    }

    if (vi_media_start(uplink, &dl_cfg, &err_code, &err_msg, &dl_failed) != BK_OK)
    {
        return doorbell_rpc_send_error(id, err_code, err_msg,
                                       dl_failed ? vi_reason("downlink") : NULL);
    }

    return doorbell_rpc_send_result_null(id);
}

/*
 * doorbell.videoIntercom.turnOff - stop both directions. Safe to call regardless
 * of the current mode.
 */
bk_err_t doorbell_rpc_video_intercom_turn_off(cJSON *params, cJSON *id)
{
    (void)params;

    vi_media_stop();
    LOGI("videoIntercom.turnOff: uplink + downlink down\n");
    return doorbell_rpc_send_result_null(id);
}

#else /* CONFIG_DOOR_ACCESS_CALL_FLOW */
/* ===================== Indoor-display incoming-call flow ===================== */

/* Single writer for the RPC-thread side (turnOn/turnOff). answer/hangup are
 * invoked from the local UI worker thread; the handful of shared fields are only
 * ever transitioned through a small state machine and the media start/stop calls
 * are internally serialized, so a plain static guarded by state checks is
 * sufficient for this cooperative flow. */
static doorbell_call_state_t   s_call_state = DOORBELL_CALL_IDLE;
static cJSON                  *s_pending_uplink = NULL;   /* deep copy of invite uplink params */
static doorbell_downlink_h264_config_t s_pending_dl;      /* parsed invite downlink cfg */
/* Deferred-reply: deep copy of the videoIntercom.turnOn request "id". The invite
 * RPC intentionally does NOT reply when received; the reply is sent later - a
 * success result on local answer, or an error on local decline / remote cancel -
 * so the App's turnOn call resolves exactly like the video_intercom protocol
 * (result = opened, error = failed), without any new message format. The
 * dispatcher frees the request root (and its id) right after the handler
 * returns, so we must own our own copy. */
static cJSON                  *s_pending_id = NULL;
static doorbell_call_event_cb_t s_call_evt_cb = NULL;
static void                   *s_call_evt_user = NULL;

static void vi_fire_event(doorbell_call_event_t evt)
{
    doorbell_call_event_cb_t cb = s_call_evt_cb;
    if (cb != NULL)
    {
        cb(evt, s_call_evt_user);
    }
}

/* Consume the pending invite id and reply success (result:null). Atomically
 * takes ownership so only the first of a concurrent answer / remote-cancel wins
 * (turnOff runs on the control thread, answer/hangup on the UI worker thread). */
static void vi_reply_pending_result(void)
{
    cJSON *pid = s_pending_id;
    s_pending_id = NULL;
    if (pid != NULL)
    {
        (void)doorbell_rpc_send_result_null(pid);
        cJSON_Delete(pid);
    }
}

/* Consume the pending invite id and reply error. Takes ownership of @p data
 * (forwarded to doorbell_rpc_send_error, or freed here if there is no pending
 * id to answer). */
static void vi_reply_pending_error(int code, const char *message, cJSON *data)
{
    cJSON *pid = s_pending_id;
    s_pending_id = NULL;
    if (pid != NULL)
    {
        (void)doorbell_rpc_send_error(pid, code, message, data);
        cJSON_Delete(pid);
    }
    else if (data != NULL)
    {
        cJSON_Delete(data);
    }
}

static void vi_clear_pending(void)
{
    if (s_pending_uplink != NULL)
    {
        cJSON_Delete(s_pending_uplink);
        s_pending_uplink = NULL;
    }
    /* Safety net: a still-pending id here means it was never answered (should
     * not normally happen - the state transitions reply first). Drop it without
     * sending anything to avoid leaking the cJSON copy. */
    if (s_pending_id != NULL)
    {
        cJSON_Delete(s_pending_id);
        s_pending_id = NULL;
    }
}

void doorbell_call_set_event_cb(doorbell_call_event_cb_t cb, void *user)
{
    s_call_evt_cb = cb;
    s_call_evt_user = user;
}

doorbell_call_state_t doorbell_call_state_get(void)
{
    return s_call_state;
}

bk_err_t doorbell_call_answer(void)
{
    int err_code = DB_RPC_ERR_INTERNAL;
    const char *err_msg = "intercom start failed";

    if (s_call_state != DOORBELL_CALL_RINGING)
    {
        LOGE("answer ignored: not ringing (state=%d)\n", (int)s_call_state);
        return BK_ERR_STATE;
    }

    if (vi_media_start(s_pending_uplink, &s_pending_dl, &err_code, &err_msg, NULL) != BK_OK)
    {
        LOGE("answer: media start failed (%s), ending call\n", err_msg ? err_msg : "");
        /* Media could not come up: fail the App's pending turnOn with an error,
         * exactly as the stock video_intercom turnOn would on a start failure. */
        vi_reply_pending_error(err_code, err_msg, NULL);
        vi_clear_pending();
        s_call_state = DOORBELL_CALL_IDLE;
        vi_fire_event(DOORBELL_CALL_EVT_ENDED);
        return BK_FAIL;
    }

    /* Two-way media is up: resolve the App's pending turnOn with success
     * (result:null), same as the video_intercom protocol. */
    vi_reply_pending_result();
    vi_clear_pending();
    s_call_state = DOORBELL_CALL_ACTIVE;
    LOGI("answer: two-way media up, call active\n");
    vi_fire_event(DOORBELL_CALL_EVT_ACTIVE);
    return BK_OK;
}

bk_err_t doorbell_call_hangup(void)
{
    bool was_active;

    if (s_call_state == DOORBELL_CALL_IDLE)
    {
        return BK_OK;
    }

    was_active = (s_call_state == DOORBELL_CALL_ACTIVE);
    if (was_active)
    {
        /* Call was already answered: the App's turnOn was resolved with success
         * at answer time, so there is no pending reply. Tear the media down
         * locally, then actively tell the App the device ended the call via
         * doorbell.notify.videoIntercomEnded (reason "hangup") so it updates its
         * call UI and stops sending/receiving without waiting to notice the
         * streams drop. */
        vi_media_stop();
        (void)doorbell_notify_video_intercom_ended("hangup");
    }
    else
    {
        /* Decline while ringing: the App's turnOn is still pending - fail it with
         * an error carrying {"reason":"rejected"}. Same result/error format as
         * video_intercom (no new message type). */
        vi_reply_pending_error(DB_RPC_ERR_NOT_SUPPORT, "call rejected", vi_reason("rejected"));
    }
    vi_clear_pending();
    s_call_state = DOORBELL_CALL_IDLE;
    LOGI("hangup: call ended (was_active=%d)\n", (int)was_active);
    vi_fire_event(DOORBELL_CALL_EVT_ENDED);
    return BK_OK;
}

/*
 * doorbell.videoIntercom.turnOn - INVITE (ring), deferred reply.
 *
 * Protocol is UNCHANGED from video_intercom: the request format is identical and
 * the eventual reply is a plain success result (on answer) or an error (on
 * decline / cancel / start failure) - no new message type or "status" field.
 * The only difference is timing: this handler does NOT reply on receipt. It
 * validates the request, stashes the parameters and the request id, moves to
 * RINGING and raises INCOMING to the local UI. The reply is sent later:
 *   - local answer  -> media start -> success result   (see doorbell_call_answer)
 *   - local decline -> error {"reason":"rejected"}     (see doorbell_call_hangup)
 *   - remote cancel -> error {"reason":"cancelled"}    (see turnOff below)
 *   - media failure -> error                           (see doorbell_call_answer)
 *
 * Parameter/format errors are still answered immediately (there is nothing to
 * ring for), matching the video_intercom validation replies.
 */
bk_err_t doorbell_rpc_video_intercom_turn_on(cJSON *params, cJSON *id)
{
    cJSON *uplink = params ? cJSON_GetObjectItem(params, "uplink") : NULL;
    cJSON *downlink = params ? cJSON_GetObjectItem(params, "downlink") : NULL;
    doorbell_downlink_h264_config_t dl_cfg;

    if (uplink == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Missing uplink", NULL);
    }
    if (downlink == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Missing downlink", NULL);
    }
    if (vi_parse_downlink(downlink, &dl_cfg) != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid downlink config", NULL);
    }

    if (s_call_state == DOORBELL_CALL_ACTIVE)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT, "call already active",
                                       vi_reason("busy"));
    }

    /* Stash the invite parameters and id (deep copies - the request root is freed
     * right after this handler returns). A repeated invite while ringing refreshes
     * the pending parameters/id; the superseded id is failed so the App's earlier
     * turnOn does not dangle. */
    if (s_call_state == DOORBELL_CALL_RINGING)
    {
        vi_reply_pending_error(DB_RPC_ERR_NOT_SUPPORT, "superseded by new invite", NULL);
    }
    vi_clear_pending();
    s_pending_uplink = cJSON_Duplicate(uplink, true);
    if (s_pending_uplink == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "out of memory", NULL);
    }
    /* id may be NULL only if the App sent this as a notification (no reply
     * expected); the deferred reply helpers then become no-ops. */
    s_pending_id = (id != NULL) ? cJSON_Duplicate(id, true) : NULL;
    s_pending_dl = dl_cfg;
    s_call_state = DOORBELL_CALL_RINGING;

    LOGI("videoIntercom.turnOn: incoming call (ringing), deferring reply until answer/decline\n");
    vi_fire_event(DOORBELL_CALL_EVT_INCOMING);

    /* No reply now - it is sent on answer (success) or decline/cancel (error). */
    return BK_OK;
}

/*
 * doorbell.videoIntercom.turnOff - remote hangup / cancel. Stops media if it was
 * running, resets to idle and notifies the local UI so it returns to home.
 */
bk_err_t doorbell_rpc_video_intercom_turn_off(cJSON *params, cJSON *id)
{
    (void)params;

    if (s_call_state != DOORBELL_CALL_IDLE)
    {
        if (s_call_state == DOORBELL_CALL_ACTIVE)
        {
            vi_media_stop();
        }
        else /* RINGING: App cancelled before the local user answered. */
        {
            /* Fail the still-pending turnOn so the App's invite resolves as an
             * error instead of dangling until its RPC timeout. */
            vi_reply_pending_error(DB_RPC_ERR_NOT_SUPPORT, "call cancelled",
                                   vi_reason("cancelled"));
        }
        vi_clear_pending();
        s_call_state = DOORBELL_CALL_IDLE;
        LOGI("videoIntercom.turnOff: remote ended the call\n");
        vi_fire_event(DOORBELL_CALL_EVT_ENDED);
    }

    return doorbell_rpc_send_result_null(id);
}

#endif /* CONFIG_DOOR_ACCESS_CALL_FLOW */
