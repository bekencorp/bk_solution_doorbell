#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include "cJSON.h"
#include "network_transfer.h"

#include "doorbell_jsonrpc.h"
#include "doorbell_rpc_internal.h"

#define TAG "db-rpc"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/* method-name -> handler dispatch table (exact match). */
static const db_rpc_entry_t s_rpc_table[] =
{
    { "doorbell.service.setType",              doorbell_rpc_service_set_type },
    { "doorbell.camera.turnOn",               doorbell_rpc_camera_turn_on },
    { "doorbell.camera.turnOff",              doorbell_rpc_camera_turn_off },
    { "doorbell.camera.getStatus",            doorbell_rpc_camera_get_status },
    { "doorbell.audio.turnOn",                doorbell_rpc_audio_turn_on },
    { "doorbell.audio.turnOff",               doorbell_rpc_audio_turn_off },
    { "doorbell.audio.getStatus",             doorbell_rpc_audio_get_status },
    { "doorbell.audio.setAcoustics",          doorbell_rpc_audio_set_acoustics },
    { "doorbell.lcd.turnOn",                  doorbell_rpc_lcd_turn_on },
    { "doorbell.lcd.turnOff",                 doorbell_rpc_lcd_turn_off },
    { "doorbell.lcd.getStatus",               doorbell_rpc_lcd_get_status },
    { "doorbell.videoIntercom.turnOn",        doorbell_rpc_video_intercom_turn_on },
    { "doorbell.videoIntercom.turnOff",       doorbell_rpc_video_intercom_turn_off },
    { "doorbell.misc.ping",                   doorbell_rpc_misc_ping },
    { "doorbell.solution.getConfig",          doorbell_rpc_solution_get_config },
};

static cJSON *db_rpc_new_msg(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root != NULL)
    {
        cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    }
    return root;
}

/* Serialize @p root, send over the JSON-framed control channel, then free. */
static bk_err_t db_rpc_send_json(cJSON *root)
{
    char *text;
    uint32_t len;
    int ret;

    if (root == NULL)
    {
        return BK_FAIL;
    }

    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (text == NULL)
    {
        LOGE("%s: print failed\n", __func__);
        return BK_FAIL;
    }

    len = (uint32_t)os_strlen(text);
    /* Log outgoing JSON so CLI loopback self-test (no network send) can still
     * observe result/error responses on the serial console. */
    LOGI("TX: %s\n", text);
    ret = ntwk_trans_ctrl_send((uint8_t *)text, len);
    cJSON_free(text);

    if (ret < 0)
    {
        LOGW("%s: ctrl_send failed, ret=%d\n", __func__, ret);
        return BK_FAIL;
    }
    return BK_OK;
}

static void db_rpc_attach_id(cJSON *root, cJSON *id)
{
    if (id != NULL)
    {
        cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, 1));
    }
    else
    {
        cJSON_AddNullToObject(root, "id");
    }
}

bk_err_t doorbell_rpc_send_result(cJSON *id, cJSON *result)
{
    cJSON *root = db_rpc_new_msg();
    if (root == NULL)
    {
        if (result != NULL)
        {
            cJSON_Delete(result);
        }
        return BK_FAIL;
    }

    if (result != NULL)
    {
        cJSON_AddItemToObject(root, "result", result);
    }
    else
    {
        cJSON_AddNullToObject(root, "result");
    }

    db_rpc_attach_id(root, id);
    return db_rpc_send_json(root);
}

bk_err_t doorbell_rpc_send_result_null(cJSON *id)
{
    return doorbell_rpc_send_result(id, NULL);
}

bk_err_t doorbell_rpc_send_status(cJSON *id, const char *status)
{
    cJSON *result = cJSON_CreateObject();
    if (result == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "no mem", NULL);
    }
    cJSON_AddStringToObject(result, "status", status ? status : "off");
    return doorbell_rpc_send_result(id, result);
}

bk_err_t doorbell_rpc_send_error(cJSON *id, int code, const char *message, cJSON *data)
{
    cJSON *root;
    cJSON *err;

    root = db_rpc_new_msg();
    if (root == NULL)
    {
        if (data != NULL)
        {
            cJSON_Delete(data);
        }
        return BK_FAIL;
    }

    err = cJSON_CreateObject();
    if (err == NULL)
    {
        if (data != NULL)
        {
            cJSON_Delete(data);
        }
        cJSON_Delete(root);
        return BK_FAIL;
    }

    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message ? message : "");
    if (data != NULL)
    {
        cJSON_AddItemToObject(err, "data", data);
    }

    cJSON_AddItemToObject(root, "error", err);
    db_rpc_attach_id(root, id);
    return db_rpc_send_json(root);
}

bk_err_t doorbell_jsonrpc_send_notify(const char *method, void *params)
{
    cJSON *root = db_rpc_new_msg();
    if (root == NULL)
    {
        if (params != NULL)
        {
            cJSON_Delete((cJSON *)params);
        }
        return BK_FAIL;
    }

    cJSON_AddStringToObject(root, "method", method ? method : "");
    if (params != NULL)
    {
        cJSON_AddItemToObject(root, "params", (cJSON *)params);
    }
    return db_rpc_send_json(root);
}

bk_err_t doorbell_notify_request_keyframe(const char *reason, const char *image_format)
{
    cJSON *params = cJSON_CreateObject();
    if (params == NULL)
    {
        return BK_FAIL;
    }
    cJSON_AddStringToObject(params, "reason", (reason != NULL) ? reason : "frameLoss");
    cJSON_AddStringToObject(params, "imageFormat", (image_format != NULL) ? image_format : "h264");
    /* send_notify takes ownership of params. */
    return doorbell_jsonrpc_send_notify("doorbell.notify.requestKeyFrame", params);
}

static void db_rpc_dispatch_request(const char *method, cJSON *params, cJSON *id)
{
    uint32_t i;

    for (i = 0; i < sizeof(s_rpc_table) / sizeof(s_rpc_table[0]); i++)
    {
        if (os_strcmp(method, s_rpc_table[i].method) == 0)
        {
            s_rpc_table[i].fn(params, id);
            return;
        }
    }

    LOGW("%s: method not found: %s\n", __func__, method);
    /* Only answer method-not-found for requests (with id). Notifications
     * (no id) are silently ignored per JSON-RPC 2.0 semantics. */
    if (id != NULL)
    {
        doorbell_rpc_send_error(id, DB_RPC_ERR_METHOD, "method not found", NULL);
    }
}

void doorbell_jsonrpc_handle_cmd(const char *json, uint32_t length)
{
    cJSON *root;
    cJSON *method;
    cJSON *params;
    cJSON *id;

    if (json == NULL || length == 0)
    {
        LOGE("%s: empty json\n", __func__);
        return;
    }

    root = cJSON_Parse(json);
    if (root == NULL)
    {
        LOGE("%s: parse error\n", __func__);
        doorbell_rpc_send_error(NULL, DB_RPC_ERR_PARSE, "parse error", NULL);
        return;
    }

    method = cJSON_GetObjectItem(root, "method");
    if (method != NULL && cJSON_IsString(method))
    {
        params = cJSON_GetObjectItem(root, "params");
        id = cJSON_GetObjectItem(root, "id");
        LOGD("%s: method=%s%s\n", __func__, method->valuestring, id ? " (request)" : " (notify)");
        db_rpc_dispatch_request(method->valuestring, params, id);
    }
    else
    {
        /* No method field => this is a response from the peer (e.g. to our
         * notification). Nothing to do on the device side. */
        LOGD("%s: peer response, ignored\n", __func__);
    }

    cJSON_Delete(root);
}
