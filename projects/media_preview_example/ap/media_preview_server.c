#include <stdio.h>
#include <string.h>
#include <os/os.h>
#include <os/str.h>
#include <components/log.h>
#include <components/netif.h>
#include "cli.h"
#include "cJSON.h"
#include "common/network_transfer_common.h"
#include "network_transfer.h"
#include "network_type.h"
#include "ntwk_sdp.h"
#include "h264e_stream_project.h"
#include "isp_frame_project.h"
#include "h264e_stream_priv.h"
#include "isp_frame_priv.h"
#include "media_preview_server.h"

#define TAG "media_preview"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define MEDIA_PREVIEW_JSON_MAX_LEN 2048

typedef enum
{
    MEDIA_PREVIEW_MODULE_NONE = 0,
    MEDIA_PREVIEW_MODULE_H264E,
    MEDIA_PREVIEW_MODULE_ISP,
} media_preview_module_t;

typedef struct
{
    uint8_t started;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    media_preview_module_t active_module;
} media_preview_server_ctx_t;

static media_preview_server_ctx_t s_media_preview_ctx = {
    .width = 1920,
    .height = 1080,
    .fps = 25,
};

static bk_err_t media_preview_validate_resolution(uint16_t width, uint16_t height)
{
    if ((width == 1280 && height == 720) ||
        (width == 1920 && height == 1080)) {
        return BK_OK;
    }

    LOGE("unsupported resolution %ux%u (supported: 1280x720, 1920x1080)\n", width, height);
    return BK_ERR_PARAM;
}

static void media_preview_log_netif_ip(const char *name, netif_if_t netif)
{
    netif_ip4_config_t ip4_config = {0};
    bk_err_t ret = bk_netif_get_ip4_config(netif, &ip4_config);

    if (ret != BK_OK || ip4_config.ip[0] == '\0' || os_strcmp(ip4_config.ip, "0.0.0.0") == 0)
    {
        LOGI("  %s IP: not ready\n", name);
        return;
    }

    LOGI("  %s IP: %s mask=%s gateway=%s\n",
         name, ip4_config.ip, ip4_config.mask, ip4_config.gateway);
}

static int media_preview_has_prefix(const char *value, const char *prefix)
{
    size_t prefix_len;

    if (value == NULL || prefix == NULL)
    {
        return 0;
    }

    prefix_len = strlen(prefix);
    return strncmp(value, prefix, prefix_len) == 0;
}

static int media_preview_is_isp_method(const char *method)
{
    return media_preview_has_prefix(method, "ispFrame.") ||
           media_preview_has_prefix(method, "doorbell.isp.") ||
           media_preview_has_prefix(method, "isp.") ||
           media_preview_has_prefix(method, "isp_capture.") ||
           media_preview_has_prefix(method, "capture.isp.") ||
           media_preview_has_prefix(method, "media.isp.");
}

static int media_preview_is_h264e_method(const char *method)
{
    return media_preview_has_prefix(method, "h264EScream.") ||
           media_preview_has_prefix(method, "doorbell.config.") ||
           media_preview_has_prefix(method, "doorbell.encoder.") ||
           media_preview_has_prefix(method, "doorbell.camera.") ||
           media_preview_has_prefix(method, "h264e.") ||
           media_preview_has_prefix(method, "h264.") ||
           media_preview_has_prefix(method, "encode_preview.") ||
           media_preview_has_prefix(method, "encoder.") ||
           strcmp(method, "start_encode") == 0 ||
           strcmp(method, "stop_encode") == 0 ||
           strcmp(method, "device:stop-preview") == 0 ||
           strcmp(method, "force_idr") == 0 ||
           strcmp(method, "get_rate_ctrl") == 0 ||
           strcmp(method, "set_rate_ctrl") == 0;
}

static int media_preview_is_generic_method(const char *method)
{
    return strcmp(method, "get_config_schema") == 0 ||
           strcmp(method, "get_config") == 0 ||
           strcmp(method, "set_config") == 0;
}

static int media_preview_is_stop_method(const char *method, media_preview_module_t module)
{
    if (module == MEDIA_PREVIEW_MODULE_ISP)
    {
        return strcmp(method, "stop_encode") == 0 ||
               strcmp(method, "device:stop-preview") == 0;
    }

    if (module == MEDIA_PREVIEW_MODULE_H264E)
    {
        return strcmp(method, "stop_encode") == 0 ||
               strcmp(method, "device:stop-preview") == 0 ||
               strcmp(method, "h264e.stop") == 0;
    }

    return 0;
}

static media_preview_module_t media_preview_classify_method(const char *method)
{
    if (media_preview_is_isp_method(method))
    {
        return MEDIA_PREVIEW_MODULE_ISP;
    }

    if ((strcmp(method, "stop_encode") == 0 ||
         strcmp(method, "device:stop-preview") == 0) &&
        s_media_preview_ctx.active_module != MEDIA_PREVIEW_MODULE_NONE)
    {
        return s_media_preview_ctx.active_module;
    }

    if (media_preview_is_h264e_method(method))
    {
        return MEDIA_PREVIEW_MODULE_H264E;
    }

    if (media_preview_is_generic_method(method) &&
        s_media_preview_ctx.active_module == MEDIA_PREVIEW_MODULE_ISP)
    {
        return MEDIA_PREVIEW_MODULE_ISP;
    }

    if (media_preview_is_generic_method(method))
    {
        return MEDIA_PREVIEW_MODULE_H264E;
    }

    return MEDIA_PREVIEW_MODULE_NONE;
}

static int media_preview_send_jsonrpc_error(const char *id_json, int code, const char *message)
{
    char response[256];
    int len;
    const char *safe_id = (id_json != NULL) ? id_json : "null";
    const char *safe_message = (message != NULL) ? message : "Method not found";

    len = snprintf(response, sizeof(response),
                   "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":%s}\n",
                   code, safe_message, safe_id);
    if (len < 0 || len >= (int)sizeof(response))
    {
        return BK_FAIL;
    }

    return ntwk_trans_ctrl_send((uint8_t *)response, (uint32_t)len);
}

static int media_preview_send_jsonrpc_result(const char *id_json, const char *server)
{
    char response[160];
    int len;
    const char *safe_id = (id_json != NULL) ? id_json : "null";
    const char *safe_server = (server != NULL) ? server : "";

    len = snprintf(response, sizeof(response),
                   "{\"jsonrpc\":\"2.0\",\"result\":{\"ok\":true,\"server\":\"%s\"},\"id\":%s}\n",
                   safe_server, safe_id);
    if (len < 0 || len >= (int)sizeof(response))
    {
        return BK_FAIL;
    }

    return ntwk_trans_ctrl_send((uint8_t *)response, (uint32_t)len);
}

static char *media_preview_jsonrpc_id_to_string(cJSON *id)
{
    if (id == NULL)
    {
        return NULL;
    }
    return cJSON_PrintUnformatted(id);
}

static bk_err_t media_preview_stop_active_module(void)
{
    if (s_media_preview_ctx.active_module == MEDIA_PREVIEW_MODULE_H264E)
    {
        (void)h264e_stream_project_module_stop();
    }
    else if (s_media_preview_ctx.active_module == MEDIA_PREVIEW_MODULE_ISP)
    {
        (void)isp_frame_project_module_stop();
    }

    s_media_preview_ctx.active_module = MEDIA_PREVIEW_MODULE_NONE;
    return BK_OK;
}

static bk_err_t media_preview_ensure_module(media_preview_module_t module)
{
    bk_err_t ret;

    if (module == MEDIA_PREVIEW_MODULE_NONE)
    {
        return BK_ERR_PARAM;
    }

    if (s_media_preview_ctx.active_module == module)
    {
        return BK_OK;
    }

    (void)media_preview_stop_active_module();

    if (module == MEDIA_PREVIEW_MODULE_H264E)
    {
        ret = h264e_stream_project_module_init(s_media_preview_ctx.width,
                                             s_media_preview_ctx.height,
                                             s_media_preview_ctx.fps);
    }
    else
    {
        ret = isp_frame_project_module_init(s_media_preview_ctx.width,
                                           s_media_preview_ctx.height,
                                           s_media_preview_ctx.fps);
    }

    if (ret == BK_OK)
    {
        s_media_preview_ctx.active_module = module;
        LOGI("active module=%s\n",
             module == MEDIA_PREVIEW_MODULE_H264E ? "h264e" : "isp");
    }

    return ret;
}

static media_preview_module_t media_preview_module_from_params(cJSON *params,
                                                              const char **server)
{
    cJSON *server_json;

    if (server != NULL)
    {
        *server = NULL;
    }

    if (params == NULL || !cJSON_IsObject(params))
    {
        return MEDIA_PREVIEW_MODULE_NONE;
    }

    server_json = cJSON_GetObjectItem(params, "server");
    if (!cJSON_IsString(server_json) || server_json->valuestring == NULL)
    {
        return MEDIA_PREVIEW_MODULE_NONE;
    }

    if (server != NULL)
    {
        *server = server_json->valuestring;
    }

    if (strcmp(server_json->valuestring, "h264") == 0)
    {
        return MEDIA_PREVIEW_MODULE_H264E;
    }
    if (strcmp(server_json->valuestring, "isp") == 0)
    {
        return MEDIA_PREVIEW_MODULE_ISP;
    }

    return MEDIA_PREVIEW_MODULE_NONE;
}

static int media_preview_handle_media_start(cJSON *params, const char *id_json)
{
    const char *server = NULL;
    media_preview_module_t module = media_preview_module_from_params(params, &server);

    if (module == MEDIA_PREVIEW_MODULE_NONE)
    {
        return media_preview_send_jsonrpc_error(id_json, -32602, "invalid server");
    }

    if (media_preview_ensure_module(module) != BK_OK)
    {
        return media_preview_send_jsonrpc_error(id_json, -32002, "module start failed");
    }

    return media_preview_send_jsonrpc_result(id_json, server);
}

static int media_preview_handle_media_stop(cJSON *params, const char *id_json)
{
    const char *server = NULL;
    media_preview_module_t module = media_preview_module_from_params(params, &server);

    if (module == MEDIA_PREVIEW_MODULE_NONE)
    {
        return media_preview_send_jsonrpc_error(id_json, -32602, "invalid server");
    }

    if (s_media_preview_ctx.active_module != module)
    {
        return media_preview_send_jsonrpc_error(id_json, -32003, "server is not active");
    }

    (void)media_preview_stop_active_module();
    return media_preview_send_jsonrpc_result(id_json, server);
}

static int media_preview_dispatch(uint8_t *data, uint32_t length)
{
    cJSON *root;
    cJSON *method;
    char *id_json;
    const char *method_string;
    media_preview_module_t module;
    int ret;

    if (data == NULL || length == 0 || length > MEDIA_PREVIEW_JSON_MAX_LEN)
    {
        return media_preview_send_jsonrpc_error(NULL, -32700, "invalid length");
    }

    root = cJSON_ParseWithLength((const char *)data, length);
    if (root == NULL)
    {
        return media_preview_send_jsonrpc_error(NULL, -32700, "parse failed");
    }

    id_json = media_preview_jsonrpc_id_to_string(cJSON_GetObjectItem(root, "id"));
    method = cJSON_GetObjectItem(root, "method");
    if (!cJSON_IsString(method) || method->valuestring == NULL)
    {
        ret = media_preview_send_jsonrpc_error(id_json, -32600, "Invalid Request");
        goto exit;
    }

    method_string = method->valuestring;
    if (strcmp(method_string, "media.start") == 0)
    {
        ret = media_preview_handle_media_start(cJSON_GetObjectItem(root, "params"), id_json);
        goto exit;
    }

    if (strcmp(method_string, "media.stop") == 0)
    {
        ret = media_preview_handle_media_stop(cJSON_GetObjectItem(root, "params"), id_json);
        goto exit;
    }

    module = media_preview_classify_method(method_string);
    if (module == MEDIA_PREVIEW_MODULE_NONE)
    {
        LOGE("unknown method: %s\n", method_string);
        ret = media_preview_send_jsonrpc_error(id_json, -32601, "Method not found");
        goto exit;
    }

    if (s_media_preview_ctx.active_module == MEDIA_PREVIEW_MODULE_NONE)
    {
        ret = media_preview_send_jsonrpc_error(id_json, -32003, "server not started");
        goto exit;
    }

    if (s_media_preview_ctx.active_module != module)
    {
        ret = media_preview_send_jsonrpc_error(id_json, -32003, "method does not match active server");
        goto exit;
    }

    if (module == MEDIA_PREVIEW_MODULE_ISP)
    {
        ret = isp_frame_protocol_handle(data, length);
    }
    else
    {
        ret = h264e_stream_protocol_handle(data, length);
    }

    if (media_preview_is_stop_method(method_string, module))
    {
        (void)media_preview_stop_active_module();
        LOGI("module stopped by PC method=%s\n", method_string);
    }

exit:
    if (id_json != NULL)
    {
        cJSON_free(id_json);
    }
    cJSON_Delete(root);
    return ret;
}

static void media_preview_network_event(ntwk_trans_event_t *event)
{
    if (event == NULL)
    {
        return;
    }
    LOGI("chan=%d event=%d param=%d\n", event->chan_type, event->code, event->param);
}

static int media_preview_ctrl_recv(uint8_t *data, uint32_t length)
{
    return media_preview_dispatch(data, length);
}

static int media_preview_video_recv(uint8_t *data, uint32_t length)
{
    (void)data;
    (void)length;
    return 0;
}

static void media_preview_log_connect_info(void)
{
    LOGI("[Media preview] PC connect info:\n");
    media_preview_log_netif_ip("STA", NETIF_IF_STA);
    media_preview_log_netif_ip("AP", NETIF_IF_AP);
    LOGI("  CTRL:  TCP %u JSON-RPC (H264E or ISP methods)\n", NTWK_TRANS_CMD_PORT);
    LOGI("  VIDEO: TCP %u H264 ES or ISP frames\n", NTWK_TRANS_TCP_VIDEO_PORT);
    LOGI("  PC selects module by method, stop method releases current module\n");
}

bk_err_t media_preview_server_start(uint16_t width, uint16_t height, uint16_t fps)
{
    bk_err_t ret;

    if (s_media_preview_ctx.started)
    {
        return BK_OK;
    }

    ret = media_preview_validate_resolution(width, height);
    if (ret != BK_OK)
    {
        return ret;
    }

    s_media_preview_ctx.width = width;
    s_media_preview_ctx.height = height;
    s_media_preview_ctx.fps = fps ? fps : 25;

    ret = bk_tcp_trans_service_init("media_preview_tcp_service");
    if (ret != BK_OK)
    {
        LOGE("network transfer service init failed: %d\n", ret);
        return ret;
    }

    ntwk_trans_register_msg_event_cb(media_preview_network_event);
    ntwk_trans_register_ctrl_recv_cb(media_preview_ctrl_recv);
    ntwk_trans_register_video_recv_cb(media_preview_video_recv);

    ret = ntwk_trans_chan_start(NTWK_TRANS_CHAN_CTRL, NULL);
    if (ret != BK_OK)
    {
        LOGE("ctrl channel start failed: %d\n", ret);
        goto fail;
    }

    ret = ntwk_trans_chan_start(NTWK_TRANS_CHAN_VIDEO, NULL);
    if (ret != BK_OK)
    {
        LOGE("video channel start failed: %d\n", ret);
        ntwk_trans_chan_stop(NTWK_TRANS_CHAN_CTRL);
        goto fail;
    }

    ret = ntwk_sdp_start("beken-media-tools",
                         NTWK_TRANS_CMD_PORT,
                         NTWK_TRANS_TCP_VIDEO_PORT,
                         NTWK_TRANS_TCP_AUDIO_PORT);
    if (ret != BK_OK)
    {
        LOGW("sdp start failed: %d\n", ret);
    }

    s_media_preview_ctx.started = 1;
    LOGI("server started, default resolution=%ux%u fps=%u\n",
         s_media_preview_ctx.width, s_media_preview_ctx.height, s_media_preview_ctx.fps);
    media_preview_log_connect_info();
    return BK_OK;

fail:
    bk_tcp_trans_service_deinit();
    return ret;
}

bk_err_t media_preview_server_stop(void)
{
    if (!s_media_preview_ctx.started)
    {
        return BK_OK;
    }

    (void)media_preview_stop_active_module();
    ntwk_sdp_stop();
    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_VIDEO);
    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_CTRL);
    bk_tcp_trans_service_deinit();
    s_media_preview_ctx.started = 0;
    LOGI("server stopped\n");
    return BK_OK;
}

static void cli_media_preview_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint16_t width = s_media_preview_ctx.width;
    uint16_t height = s_media_preview_ctx.height;
    uint16_t fps = s_media_preview_ctx.fps;
    bk_err_t ret;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2)
    {
        LOGI("media_preview start [720p|1080p] [fps] | stop\n");
        return;
    }

    if (os_strcmp(argv[1], "stop") == 0)
    {
        media_preview_server_stop();
        return;
    }

    if (os_strcmp(argv[1], "start") != 0)
    {
        LOGI("usage: media_preview start [720p|1080p] [fps] | stop\n");
        return;
    }

    for (int i = 2; i < argc; ++i)
    {
        if (os_strcmp(argv[i], "1080p") == 0)
        {
            width = 1920;
            height = 1080;
        }
        else if (os_strcmp(argv[i], "720p") == 0)
        {
            width = 1280;
            height = 720;
        }
        else
        {
            fps = (uint16_t)os_strtoul(argv[i], NULL, 10);
            if (fps == 0)
            {
                fps = 25;
            }
        }
    }

    ret = media_preview_server_start(width, height, fps);
    if (ret != BK_OK)
    {
        LOGE("server start failed: %d\n", ret);
    }
}

static const struct cli_command s_media_preview_commands[] =
{
    {"media_preview", "Unified media preview server start|stop", cli_media_preview_cmd},
};

int media_preview_server_cli_init(void)
{
    return cli_register_commands(s_media_preview_commands,
                                 sizeof(s_media_preview_commands) / sizeof(s_media_preview_commands[0]));
}
