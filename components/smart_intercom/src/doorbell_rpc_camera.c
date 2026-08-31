#include <common/bk_include.h>
#include <os/str.h>
#include <os/os.h>

#include "cJSON.h"

#include "doorbell_comm.h"
#include "doorbell_cmd.h"
#include "doorbell_devices.h"
#include "doorbell_audio_device.h"
#include "doorbell_rpc_internal.h"

#define TAG "db-rpc-cam"
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Device supports one active capture path at a time -> one main stream. */
#define DB_CAMERA_MAX_STREAMS (1)

static cJSON *db_json_int(cJSON *obj, const char *key)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, key) : NULL;
    return (it != NULL && cJSON_IsNumber(it)) ? it : NULL;
}

/* Parse "videoFormat" -> camera_parameters_t.format (1=h264, 0=mjpeg). */
static int db_parse_video_format(cJSON *cfg, uint16_t *out_format)
{
    cJSON *vf = cfg ? cJSON_GetObjectItem(cfg, "videoFormat") : NULL;
    if (vf == NULL || !cJSON_IsString(vf))
    {
        return BK_FAIL;
    }
    if (os_strcmp(vf->valuestring, "h264") == 0)
    {
        *out_format = 1;
        return BK_OK;
    }
    if (os_strcmp(vf->valuestring, "mjpeg") == 0)
    {
        *out_format = 0;
        return BK_OK;
    }
    return BK_FAIL;
}

/* Translate one CameraStreamConfig into camera_parameters_t.
 * Returns BK_OK, or BK_FAIL on invalid params (caller replies -32602). */
static int db_parse_stream(cJSON *stream, camera_parameters_t *out)
{
    cJSON *cam_type = stream ? cJSON_GetObjectItem(stream, "cameraType") : NULL;
    cJSON *cam_cfg = stream ? cJSON_GetObjectItem(stream, "cameraConfig") : NULL;
    cJSON *cfg;
    cJSON *w;
    cJSON *h;
    cJSON *fps;

    if (cam_type == NULL || !cJSON_IsString(cam_type) || cam_cfg == NULL)
    {
        return BK_FAIL;
    }

    os_memset(out, 0, sizeof(*out));
    out->rotate = 0xFFFF; /* sensor/board default */

    if (os_strcmp(cam_type->valuestring, "mipi") == 0 ||
        os_strcmp(cam_type->valuestring, "dvp") == 0)
    {
        cfg = cJSON_GetObjectItem(cam_cfg, cam_type->valuestring);
        if (cfg == NULL)
        {
            return BK_FAIL;
        }
        w = db_json_int(cfg, "width");
        h = db_json_int(cfg, "height");
        fps = db_json_int(cfg, "fps");
        if (w == NULL || h == NULL || fps == NULL)
        {
            return BK_FAIL;
        }
        if (db_parse_video_format(cfg, &out->format) != BK_OK)
        {
            return BK_FAIL;
        }

        /* Non-UVC id selects the on-board ISP (MIPI/DVP) capture path. */
        {
            cJSON *sensor_id = db_json_int(cfg, "sensorId");
            uint16_t id = (sensor_id != NULL) ? (uint16_t)sensor_id->valueint : 1;
            if (id == UVC_DEVICE_ID)
            {
                id = 1;
            }
            out->id = id;
        }
        out->width = (uint16_t)w->valueint;
        out->height = (uint16_t)h->valueint;

        {
            cJSON *rot = db_json_int(cfg, "rotate");
            if (rot != NULL)
            {
                out->rotate = (uint16_t)rot->valueint;
            }
        }
        return BK_OK;
    }
    else if (os_strcmp(cam_type->valuestring, "uvc") == 0)
    {
        cfg = cJSON_GetObjectItem(cam_cfg, "uvc");
        if (cfg == NULL)
        {
            return BK_FAIL;
        }
        w = db_json_int(cfg, "width");
        h = db_json_int(cfg, "height");
        fps = db_json_int(cfg, "fps");
        if (w == NULL || h == NULL || fps == NULL || db_json_int(cfg, "port") == NULL)
        {
            return BK_FAIL;
        }
        if (db_parse_video_format(cfg, &out->format) != BK_OK)
        {
            return BK_FAIL;
        }
        out->id = UVC_DEVICE_ID;
        out->width = (uint16_t)w->valueint;
        out->height = (uint16_t)h->valueint;
        return BK_OK;
    }

    return BK_FAIL;
}

/* Validate a camera.turnOn-style params object ("streamCount" + "streams"[])
 * and decode the first (main) stream into @out. On failure fills *err_code /
 * *err_msg for the RPC reply and returns BK_FAIL. */
static bk_err_t db_camera_parse_first_stream(cJSON *params, camera_parameters_t *out,
                                             int *err_code, const char **err_msg)
{
    cJSON *streams = params ? cJSON_GetObjectItem(params, "streams") : NULL;
    cJSON *stream_count = params ? cJSON_GetObjectItem(params, "streamCount") : NULL;
    int n;

    *err_code = DB_RPC_ERR_PARAMS;

    if (streams == NULL || !cJSON_IsArray(streams))
    {
        *err_msg = "Invalid streams";
        return BK_FAIL;
    }

    n = cJSON_GetArraySize(streams);
    if (n <= 0)
    {
        *err_msg = "Empty streams";
        return BK_FAIL;
    }

    if (stream_count != NULL && cJSON_IsNumber(stream_count) && stream_count->valueint != n)
    {
        *err_msg = "streamCount mismatch";
        return BK_FAIL;
    }

    if (n > DB_CAMERA_MAX_STREAMS)
    {
        *err_code = DB_RPC_ERR_NOT_SUPPORT;
        *err_msg = "too many streams";
        return BK_FAIL;
    }

    if (db_parse_stream(cJSON_GetArrayItem(streams, 0), out) != BK_OK)
    {
        *err_msg = "Invalid stream config";
        return BK_FAIL;
    }

    return BK_OK;
}

/* Open the uplink (video capture + H.264 encode + video transfer) from a
 * camera.turnOn-style params object, with rollback on failure. Shared by
 * doorbell.camera.turnOn and doorbell.videoIntercom.turnOn. */
bk_err_t doorbell_rpc_camera_uplink_open_from_params(cJSON *params, int *err_code, const char **err_msg)
{
    camera_parameters_t parameters;
    int cam_ret;
    int trans_ret;
    int ret;

    if (db_camera_parse_first_stream(params, &parameters, err_code, err_msg) != BK_OK)
    {
        return BK_FAIL;
    }

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    bool camera_vote_was_set = (doorbell_mm_service_get_status() & MM_STATUS_CAMERA_MASK) != 0;
    doorbell_mm_service_vote(MM_STATUS_CAMERA_BIT, true);
#endif

#if (CONFIG_ASR_SERVICE_WITH_MIC)
    doorbell_asr_turn_off();
#endif

    cam_ret = doorbell_camera_turn_on(&parameters);
    if (cam_ret != BK_OK)
    {
        LOGE("doorbell_camera_turn_on failed\n");
    }

    trans_ret = BK_OK;
    if (cam_ret == BK_OK)
    {
        trans_ret = doorbell_video_transfer_turn_on();
        if (trans_ret != BK_OK)
        {
            LOGE("doorbell_video_transfer_turn_on failed\n");
            doorbell_camera_turn_off();
        }
    }

    ret = (cam_ret == BK_OK && trans_ret == BK_OK) ? BK_OK : BK_FAIL;

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (ret != BK_OK && !camera_vote_was_set)
    {
        doorbell_mm_service_vote(MM_STATUS_CAMERA_BIT, false);
    }
#endif

    if (ret != BK_OK)
    {
        *err_code = DB_RPC_ERR_INTERNAL;
        *err_msg = "camera turn on failed";
        return BK_FAIL;
    }
    return BK_OK;
}

/* Close the uplink (video transfer + capture) and drop the camera vote. */
bk_err_t doorbell_rpc_camera_uplink_close(void)
{
    int ret;

    doorbell_video_transfer_turn_off();
    ret = doorbell_camera_turn_off();
    if (ret != BK_OK)
    {
        LOGE("doorbell_camera_turn_off failed\n");
    }

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (ret == BK_OK)
    {
        doorbell_mm_service_vote(MM_STATUS_CAMERA_BIT, false);
    }
#endif

    return (ret == BK_OK) ? BK_OK : BK_FAIL;
}

/* doorbell.camera.turnOn : open single-direction image transfer (local MIPI
 * capture -> uplink to App + local MIPI preview). Rejected while the two-way
 * video intercom is active (that mode owns both links; see
 * doorbell.videoIntercom.turnOn/turnOff). */
bk_err_t doorbell_rpc_camera_turn_on(cJSON *params, cJSON *id)
{
    int err_code = DB_RPC_ERR_INTERNAL;
    const char *err_msg = "camera turn on failed";

    if (doorbell_rpc_work_mode_get() == DB_WORK_MODE_INTERCOM)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT,
                                       "video intercom active, use videoIntercom.turnOff", NULL);
    }

    if (doorbell_rpc_camera_uplink_open_from_params(params, &err_code, &err_msg) != BK_OK)
    {
        return doorbell_rpc_send_error(id, err_code, err_msg, NULL);
    }

    doorbell_rpc_work_mode_set(DB_WORK_MODE_SINGLE);
    return doorbell_rpc_send_result_null(id);
}

/* doorbell.camera.turnOff : params.target must be "all". Rejected while the
 * two-way video intercom is active; use doorbell.videoIntercom.turnOff instead. */
bk_err_t doorbell_rpc_camera_turn_off(cJSON *params, cJSON *id)
{
    cJSON *target = params ? cJSON_GetObjectItem(params, "target") : NULL;

    if (doorbell_rpc_work_mode_get() == DB_WORK_MODE_INTERCOM)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT,
                                       "video intercom active, use videoIntercom.turnOff", NULL);
    }

    if (target == NULL || !cJSON_IsString(target) || os_strcmp(target->valuestring, "all") != 0)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "target must be \"all\"", NULL);
    }

    if (doorbell_rpc_camera_uplink_close() != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "camera turn off failed", NULL);
    }

    doorbell_rpc_work_mode_set(DB_WORK_MODE_IDLE);
    return doorbell_rpc_send_result_null(id);
}

/* doorbell.camera.getStatus : result.status = "on" | "off". */
bk_err_t doorbell_rpc_camera_get_status(cJSON *params, cJSON *id)
{
    (void)params;
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    bool on = (doorbell_mm_service_get_status() & MM_STATUS_CAMERA_MASK) != 0;
    return doorbell_rpc_send_status(id, on ? "on" : "off");
#else
    return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "status unavailable", NULL);
#endif
}
