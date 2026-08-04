#include <common/bk_include.h>
#include <os/str.h>

#include "cJSON.h"

#include "doorbell_rpc_internal.h"
#include "doorbell_downlink_video.h"

#define TAG "db-rpc-img"
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static cJSON *db_json_int(cJSON *obj, const char *key)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, key) : NULL;
    return (it != NULL && cJSON_IsNumber(it)) ? it : NULL;
}

/* doorbell.imageStream.setReceiveConfig : configure the downlink (app -> device)
 * image stream that is decoded and shown as the main picture. */
bk_err_t doorbell_rpc_image_set_receive_config(cJSON *params, cJSON *id)
{
    cJSON *image_format = params ? cJSON_GetObjectItem(params, "imageFormat") : NULL;
    cJSON *format_config = params ? cJSON_GetObjectItem(params, "formatConfig") : NULL;

    if (image_format == NULL || !cJSON_IsString(image_format) || format_config == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid receive config", NULL);
    }

    if (os_strcmp(image_format->valuestring, "h264") == 0)
    {
        cJSON *h264 = cJSON_GetObjectItem(format_config, "h264");
        cJSON *w = db_json_int(h264, "width");
        cJSON *h = db_json_int(h264, "height");
        cJSON *fps = db_json_int(h264, "fps");
        cJSON *pfc = db_json_int(h264, "pFrameCount");
        doorbell_downlink_h264_config_t cfg = {0};
        bk_err_t ret;

        if (w == NULL || h == NULL)
        {
            return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Missing h264 width/height", NULL);
        }

        cfg.width = (uint16_t)w->valueint;
        cfg.height = (uint16_t)h->valueint;
        cfg.fps = (fps != NULL) ? (uint16_t)fps->valueint : 0;
        cfg.p_frame_count = (pfc != NULL) ? (uint16_t)pfc->valueint : 0;

        ret = doorbell_downlink_set_h264_receive_config(&cfg);
        BK_LOGI(TAG, "setRecvCfg h264 %ux%u fps=%u pfc=%u ret=%d\n",
                (unsigned)cfg.width, (unsigned)cfg.height,
                (unsigned)cfg.fps, (unsigned)cfg.p_frame_count, (int)ret);
        if (ret != BK_OK)
        {
            return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "downlink start failed", NULL);
        }
        return doorbell_rpc_send_result_null(id);
    }

    if (os_strcmp(image_format->valuestring, "mjpeg") == 0)
    {
        cJSON *data = cJSON_CreateObject();
        if (data != NULL)
        {
            cJSON_AddStringToObject(data, "reason", "mjpeg downlink not supported yet");
        }
        return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT, "unsupported imageFormat", data);
    }

    return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid imageFormat", NULL);
}
