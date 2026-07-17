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

    /* #region agent log (debug 7ee2d5) */
    BK_LOGI(TAG, "[DBG7ee2d5][hyp-C/D] parse_stream: cameraType=%s isStr=%d cameraConfig=%s\n",
            cam_type ? (cJSON_IsString(cam_type) ? cam_type->valuestring : "<not-string>") : "<null>",
            cam_type ? cJSON_IsString(cam_type) : -1,
            cam_cfg ? "present" : "<null>");
    /* #endregion */

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
        /* #region agent log (debug 7ee2d5) */
        {
            cJSON *vf_dbg = cfg ? cJSON_GetObjectItem(cfg, "videoFormat") : NULL;
            BK_LOGI(TAG, "[DBG7ee2d5][hyp-C/D] %s cfg=%s w=%s h=%s fps=%s videoFormat=%s\n",
                    cam_type->valuestring, cfg ? "present" : "<null>",
                    db_json_int(cfg, "width") ? "ok" : "MISS/!num",
                    db_json_int(cfg, "height") ? "ok" : "MISS/!num",
                    db_json_int(cfg, "fps") ? "ok" : "MISS/!num",
                    (vf_dbg && cJSON_IsString(vf_dbg)) ? vf_dbg->valuestring : "<null/!str>");
        }
        /* #endregion */
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
        /* #region agent log (debug 7ee2d5) */
        {
            cJSON *vf_dbg = cfg ? cJSON_GetObjectItem(cfg, "videoFormat") : NULL;
            BK_LOGI(TAG, "[DBG7ee2d5][hyp-C/D] uvc cfg=%s w=%s h=%s fps=%s port=%s videoFormat=%s\n",
                    cfg ? "present" : "<null>",
                    db_json_int(cfg, "width") ? "ok" : "MISS/!num",
                    db_json_int(cfg, "height") ? "ok" : "MISS/!num",
                    db_json_int(cfg, "fps") ? "ok" : "MISS/!num",
                    db_json_int(cfg, "port") ? "ok" : "MISS/!num",
                    (vf_dbg && cJSON_IsString(vf_dbg)) ? vf_dbg->valuestring : "<null/!str>");
        }
        /* #endregion */
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

/* doorbell.camera.turnOn : open video capture + uplink transfer. */
bk_err_t doorbell_rpc_camera_turn_on(cJSON *params, cJSON *id)
{
    cJSON *streams = params ? cJSON_GetObjectItem(params, "streams") : NULL;
    cJSON *stream_count = params ? cJSON_GetObjectItem(params, "streamCount") : NULL;
    camera_parameters_t parameters;
    int n;
    int cam_ret;
    int trans_ret;
    int ret;

    if (streams == NULL || !cJSON_IsArray(streams))
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid streams", NULL);
    }

    n = cJSON_GetArraySize(streams);
    if (n <= 0)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Empty streams", NULL);
    }

    if (stream_count != NULL && cJSON_IsNumber(stream_count) && stream_count->valueint != n)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "streamCount mismatch", NULL);
    }

    if (n > DB_CAMERA_MAX_STREAMS)
    {
        cJSON *data = cJSON_CreateObject();
        if (data != NULL)
        {
            cJSON_AddNumberToObject(data, "maxStreams", DB_CAMERA_MAX_STREAMS);
        }
        return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT, "too many streams", data);
    }

    if (db_parse_stream(cJSON_GetArrayItem(streams, 0), &parameters) != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid stream config", NULL);
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
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "camera turn on failed", NULL);
    }
    return doorbell_rpc_send_result_null(id);
}

/* doorbell.camera.turnOff : params.target must be "all". */
bk_err_t doorbell_rpc_camera_turn_off(cJSON *params, cJSON *id)
{
    cJSON *target = params ? cJSON_GetObjectItem(params, "target") : NULL;
    int ret;

    if (target == NULL || !cJSON_IsString(target) || os_strcmp(target->valuestring, "all") != 0)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "target must be \"all\"", NULL);
    }

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

    if (ret != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "camera turn off failed", NULL);
    }
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
