#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>

#include "cJSON.h"

#include "doorbell_rpc_internal.h"
#include "doorbell_downlink_video.h"

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
 * doorbell.videoIntercom.turnOn
 *
 * One command opens both directions of the two-way video intercom:
 *   - uplink   : camera capture + H.264 encode + video transfer to the App
 *                (params.uplink, fields identical to doorbell.camera.turnOn)
 *   - downlink : receive + decode + display the App image stream
 *                (params.downlink, fields identical to
 *                 doorbell.imageStream.setReceiveConfig)
 *
 * Atomicity: the uplink is opened first; if the downlink then fails to start,
 * the uplink is rolled back so the device never rests in a half-open state.
 * Idempotency: a repeated call while already in intercom mode reconfigures with
 * the new parameters (tear down first, then re-open).
 */
bk_err_t doorbell_rpc_video_intercom_turn_on(cJSON *params, cJSON *id)
{
    cJSON *uplink = params ? cJSON_GetObjectItem(params, "uplink") : NULL;
    cJSON *downlink = params ? cJSON_GetObjectItem(params, "downlink") : NULL;
    doorbell_downlink_h264_config_t dl_cfg;
    int up_err_code = DB_RPC_ERR_INTERNAL;
    const char *up_err_msg = "uplink open failed";

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

    if (s_work_mode == DB_WORK_MODE_SINGLE)
    {
        /* Single-direction image transfer owns the uplink; require an explicit
         * camera.turnOff before switching into the two-way intercom. */
        return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT,
                                       "single-direction camera active, turn off first", NULL);
    }

    if (s_work_mode == DB_WORK_MODE_INTERCOM)
    {
        /* Reconfigure: tear the current session down so the new parameters take
         * effect cleanly (camera.turnOn is a no-op when already open). */
        (void)doorbell_rpc_camera_uplink_close();
        (void)doorbell_downlink_video_stop();
        s_work_mode = DB_WORK_MODE_IDLE;
    }

    /* Open uplink first. */
    if (doorbell_rpc_camera_uplink_open_from_params(uplink, &up_err_code, &up_err_msg) != BK_OK)
    {
        LOGE("videoIntercom.turnOn: uplink open failed (%s)\n", up_err_msg ? up_err_msg : "");
        return doorbell_rpc_send_error(id, up_err_code, up_err_msg, NULL);
    }

    /* Then bring up the downlink; roll the uplink back on failure. */
    if (doorbell_downlink_set_h264_receive_config(&dl_cfg) != BK_OK)
    {
        LOGE("videoIntercom.turnOn: downlink open failed, rolling back uplink\n");
        (void)doorbell_rpc_camera_uplink_close();
        return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT, "downlink open failed",
                                       vi_reason("downlink"));
    }

    s_work_mode = DB_WORK_MODE_INTERCOM;
    LOGI("videoIntercom.turnOn: uplink + downlink %ux%u up\n",
         (unsigned)dl_cfg.width, (unsigned)dl_cfg.height);
    return doorbell_rpc_send_result_null(id);
}

/*
 * doorbell.videoIntercom.turnOff
 *
 * Stop both directions of the two-way video intercom. Uplink is closed before
 * the downlink so the compositor drops the local self-view (PIP) while its ISP
 * SP source is still valid, then the downlink decode/display pipeline is torn
 * down. Safe to call regardless of the current mode.
 */
bk_err_t doorbell_rpc_video_intercom_turn_off(cJSON *params, cJSON *id)
{
    (void)params;

    (void)doorbell_rpc_camera_uplink_close();
    (void)doorbell_downlink_video_stop();

    s_work_mode = DB_WORK_MODE_IDLE;
    LOGI("videoIntercom.turnOff: uplink + downlink down\n");
    return doorbell_rpc_send_result_null(id);
}
