#include <os/os.h>
#include <os/str.h>
#include <components/log.h>
#include <components/netif.h>
#include "cli.h"
#include "driver/gpio.h"
#include <common/avdk_pixel_types.h>
#include <components/bk_flexa_bond.h>
#include <components/bk_encode/bk_h264_encode_ctlr.h>
#include "avdk_error.h"
#include "network_type.h"

#include "app_camera.h"
#include "app_codec.h"
#include "h264e_stream_session.h"
#include "h264e_stream_project.h"
#include "devices_mgmt.h"
#define TAG "h264e_stream_proj"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

typedef struct
{
    uint8_t server_started;
    uint8_t encode_started;
    uint16_t fps;
    uint16_t width;
    uint16_t height;
    uint32_t bitrate_kbps;
    uint32_t gop_frame_count;
    uint8_t camera_started;
    void *isp_handle;
    bk_h264_encode_ctlr_handle_t enc_handle;
    void *h264e_bond;
} h264e_stream_project_ctx_t;

static h264e_stream_project_ctx_t s_project_ctx = {
    .fps = 20,
    .width = 2304,
    .height = 1296,
    .bitrate_kbps = 1200,
    .gop_frame_count = 20,
};

static void h264e_stream_project_log_json_chunks(const char *prefix, const char *json)
{
    const char *p = json;
    char chunk[161];

    if (prefix != NULL) {
        LOGI("%s\n", prefix);
    }

    if (json == NULL) {
        LOGI("null\n");
        return;
    }

    while (*p != '\0') {
        size_t len = os_strlen(p);
        if (len > sizeof(chunk) - 1) {
            len = sizeof(chunk) - 1;
        }
        os_memcpy(chunk, p, len);
        chunk[len] = '\0';
        LOGI("%s\n", chunk);
        p += len;
    }
}

static void h264e_stream_project_log_mapped_vcenc_rate_ctrl(const bk_h264_encode_rate_ctrl_t *rate_ctrl)
{
    LOGI("  vcencRateCtrl.bitPerSecond=%u\n", rate_ctrl->bitrate);
    LOGI("  vcencRateCtrl.qpMinI=%u\n", rate_ctrl->qp_min_i);
    LOGI("  vcencRateCtrl.qpMaxI=%u\n", rate_ctrl->qp_max_i);
    LOGI("  vcencRateCtrl.qpMinPB=%u\n", rate_ctrl->qp_min_p);
    LOGI("  vcencRateCtrl.qpMaxPB=%u\n", rate_ctrl->qp_max_p);
    LOGI("  vcencRateCtrl.note=legacy rateCtrl view; run h264e_stream_config_get for full VCEnc fields\n");
}

static void h264e_stream_project_log_netif_ip(const char *name, netif_if_t netif)
{
    netif_ip4_config_t ip4_config = {0};
    bk_err_t ret = bk_netif_get_ip4_config(netif, &ip4_config);

    if (ret != BK_OK || ip4_config.ip[0] == '\0' || os_strcmp(ip4_config.ip, "0.0.0.0") == 0) {
        LOGI("  %s IP: not ready\n", name);
        return;
    }

    LOGI("  %s IP: %s mask=%s gateway=%s\n",
         name, ip4_config.ip, ip4_config.mask, ip4_config.gateway);
}

static void h264e_stream_project_log_pc_h264e_connect_info(void)
{
    LOGI("[H264E module] PC connect info:\n");
    h264e_stream_project_log_netif_ip("STA", NETIF_IF_STA);
    h264e_stream_project_log_netif_ip("AP", NETIF_IF_AP);
    LOGI("  CTRL:  TCP %u JSON-RPC (rate ctrl / start_encode)\n", NTWK_TRANS_CMD_PORT);
    LOGI("  VIDEO: TCP %u H264 ES (record stream)\n", NTWK_TRANS_TCP_VIDEO_PORT);
    LOGI("  PC -> <board IP>:%u and :%u\n", NTWK_TRANS_CMD_PORT, NTWK_TRANS_TCP_VIDEO_PORT);
    LOGI("  encode needs ISP from PC turnOn or: ap_cmd isp_frame server start\n");
}

static bk_err_t h264e_stream_project_validate_resolution(uint16_t width, uint16_t height)
{
    if ((width == 2304 && height == 1296) ||
        (width == 1280 && height == 720) ||
        (width == 1920 && height == 1080)) {
        return BK_OK;
    }

    LOGE("unsupported resolution %ux%u (supported: 2304x1296, 1280x720, 1920x1080)\n", width, height);
    return BK_ERR_PARAM;
}

static void h264e_stream_project_fill_camera_config(camera_board_config_t *config,
                                                  uint16_t width, uint16_t height,
                                                  uint16_t fps)
{
    os_memset(config, 0, sizeof(*config));

    config->mipi.enable = true;
    config->mipi.pin_scl = GPIO_69;
    config->mipi.pin_sda = GPIO_70;
    config->mipi.i2c_id = 1;
    config->mipi.pin_reset = GPIO_71;
    config->mipi.pin_pwdn = -1;
    config->mipi.pin_xclk = GPIO_59;

    config->mipi.sensor_max_width = width;
    config->mipi.sensor_max_height = height;
    config->isp.mp_width = width;
    config->isp.mp_height = height;

    config->mipi.sensor_fps = fps ? fps : 20;
    config->mipi.hmirror = 0;
    config->mipi.vflip = 0;
    config->isp.mp_enable = true;
    config->isp.mp_flexa = true;
    config->isp.mp_format = BK_PIXEL_FORMAT_NV12;
    config->isp.sp_enable = false;
    config->isp.sp_flexa = false;
}

static void h264e_stream_project_parse_video_args(int argc, char **argv, int start_idx,
                                                uint16_t *width, uint16_t *height,
                                                uint16_t *fps)
{
    for (int i = start_idx; i < argc; ++i) {
        if (os_strcmp(argv[i], "1080p") == 0) {
            *width = 1920;
            *height = 1080;
        } else if (os_strcmp(argv[i], "720p") == 0) {
            *width = 1280;
            *height = 720;
        } else {
            *fps = (uint16_t)os_strtoul(argv[i], NULL, 10);
            if (*fps == 0) {
                *fps = 20;
            }
        }
    }
}

static bk_err_t h264e_stream_project_set_video_profile(uint16_t width, uint16_t height, uint16_t fps)
{
    bk_err_t ret = h264e_stream_project_validate_resolution(width, height);
    if (ret != BK_OK) {
        return ret;
    }

    s_project_ctx.width = width;
    s_project_ctx.height = height;
    s_project_ctx.fps = fps ? fps : 20;
    return BK_OK;
}

static void h264e_stream_project_sync_session_video_config(void)
{
    uint16_t width = s_project_ctx.width;
    uint16_t height = s_project_ctx.height;
    uint16_t fps = s_project_ctx.fps;
    uint32_t gop_frame_count = s_project_ctx.gop_frame_count;

    if (h264e_stream_session_get_video_config(&width, &height, &fps, &gop_frame_count) != BK_OK) {
        return;
    }
    if (h264e_stream_project_set_video_profile(width, height, fps) != BK_OK) {
        LOGE("ignore invalid session video config %ux%u fps=%u\n", width, height, fps);
        return;
    }
    s_project_ctx.gop_frame_count = gop_frame_count ? gop_frame_count : 20;
}

static bk_err_t h264e_stream_project_encode_prepare(void)
{
    bk_err_t ret;

    if (s_project_ctx.encode_started) {
        return BK_OK;
    }

    if (!s_project_ctx.server_started) {
        LOGE("h264e_stream server not started, run: ap_cmd h264e_stream server start\n");
        return BK_ERR_STATE;
    }

    h264e_stream_project_sync_session_video_config();

    s_project_ctx.isp_handle = app_isp_handle_get();
    if (s_project_ctx.isp_handle == NULL) {
        camera_board_config_t config;
        avdk_err_t camera_ret;

        h264e_stream_project_fill_camera_config(&config,
                                              s_project_ctx.width,
                                              s_project_ctx.height,
                                              s_project_ctx.fps);
        app_camera_board_config_set(&config);
        camera_ret = app_isp_mipi_camera_turn_on(app_camera_board_config_get());
        if (camera_ret != AVDK_ERR_OK) {
            LOGE("isp camera turn on failed: %d\n", camera_ret);
            return BK_FAIL;
        }

        s_project_ctx.camera_started = 1;
        s_project_ctx.isp_handle = app_isp_handle_get();
        if (s_project_ctx.isp_handle == NULL) {
            LOGE("ISP handle is NULL after camera turn on\n");
            (void)app_isp_camera_turn_off();
            s_project_ctx.camera_started = 0;
            return BK_FAIL;
        }
    }

    ret = app_h264e_turn_on();
    if (ret != BK_OK) {
        LOGE("h264e turn on failed: %d\n", ret);
        goto fail;
    }

    s_project_ctx.enc_handle = (bk_h264_encode_ctlr_handle_t)app_h264_encode_handle_get();
    if (s_project_ctx.enc_handle == NULL) {
        LOGE("encoder handle is NULL\n");
        ret = BK_FAIL;
        goto fail;
    }

    if (s_project_ctx.gop_frame_count > 0) {
        ret = bk_h264_encode_set_gop_frame_count(s_project_ctx.enc_handle, s_project_ctx.gop_frame_count);
        if (ret != AVDK_ERR_OK) {
            LOGE("set gop frame count failed: %d\n", ret);
            goto fail;
        }
    }

    ret = bk_flexa_isp_h264e_bond_start(&s_project_ctx.h264e_bond,
                                        s_project_ctx.isp_handle,
                                        s_project_ctx.enc_handle);
    if (ret != BK_OK) {
        LOGE("isp h264e bond start failed: %d\n", ret);
        goto fail;
    }

    ret = h264e_stream_session_bind_encoder(s_project_ctx.enc_handle);
    if (ret != BK_OK) {
        LOGE("bind encoder failed: %d\n", ret);
        goto fail;
    }

    s_project_ctx.encode_started = 1;
    LOGI("[H264E module] encode prepared, resolution=%ux%u fps=%u gop=%u\n",
         s_project_ctx.width, s_project_ctx.height, s_project_ctx.fps,
         s_project_ctx.gop_frame_count);
    return BK_OK;

fail:
    h264e_stream_session_unbind_encoder();
    if (s_project_ctx.h264e_bond != NULL) {
        bk_flexa_isp_h264e_bond_stop(s_project_ctx.h264e_bond);
        s_project_ctx.h264e_bond = NULL;
    }
    if (s_project_ctx.enc_handle != NULL) {
        app_h264e_turn_off();
        s_project_ctx.enc_handle = NULL;
    }

    if (s_project_ctx.camera_started) {
        (void)app_isp_camera_turn_off();
        s_project_ctx.camera_started = 0;
        s_project_ctx.isp_handle = NULL;
    }
    return ret;
}

bk_err_t h264e_stream_project_encode_start(void)
{
    bk_err_t ret = h264e_stream_project_encode_prepare();
    if (ret != BK_OK) {
        return ret;
    }

    if (s_project_ctx.enc_handle == NULL) {
        return BK_FAIL;
    }

    if (bk_h264_encode_start(s_project_ctx.enc_handle) != AVDK_ERR_OK) {
        LOGE("bk_h264_encode_start failed\n");
        h264e_stream_project_encode_stop();
        return BK_FAIL;
    }

    LOGI("[H264E module] encode running\n");
    return BK_OK;
}

bk_err_t h264e_stream_project_encode_stop(void)
{
    if (!s_project_ctx.encode_started) {
        return BK_OK;
    }

    h264e_stream_session_unbind_encoder();

    if (s_project_ctx.h264e_bond != NULL) {
        bk_flexa_isp_h264e_bond_stop(s_project_ctx.h264e_bond);
        s_project_ctx.h264e_bond = NULL;
    }

    if (s_project_ctx.enc_handle != NULL) {
        app_h264e_turn_off();
        s_project_ctx.enc_handle = NULL;
    }

    if (s_project_ctx.camera_started) {
        (void)app_isp_camera_turn_off();
        s_project_ctx.camera_started = 0;
        s_project_ctx.isp_handle = NULL;
    }

    s_project_ctx.encode_started = 0;
    LOGI("[H264E module] encode stopped\n");
    return BK_OK;
}

static bk_err_t h264e_stream_project_encode_prepare_cb(void *user_data)
{
    (void)user_data;
    return h264e_stream_project_encode_prepare();
}

static bk_err_t h264e_stream_project_encode_stop_cb(void *user_data)
{
    (void)user_data;
    return h264e_stream_project_encode_stop();
}

bk_err_t h264e_stream_project_server_start(uint16_t width, uint16_t height, uint16_t fps)
{
    bk_err_t ret;
    h264e_stream_session_config_t demo_config = {
        .service = H264E_STREAM_SESSION_SERVICE_TCP,
        .auto_force_idr = 1,
    };

    if (s_project_ctx.server_started) {
        LOGI("h264e_stream server already started\n");
        return BK_OK;
    }

    ret = h264e_stream_project_set_video_profile(width, height, fps);
    if (ret != BK_OK) {
        return ret;
    }
    s_project_ctx.bitrate_kbps = 1200;

    ret = h264e_stream_session_init(&demo_config);
    if (ret != BK_OK) {
        LOGE("h264e stream session init failed: %d\n", ret);
        goto fail;
    }

    ret = h264e_stream_session_register_media_ops(h264e_stream_project_encode_prepare_cb,
                                             h264e_stream_project_encode_stop_cb,
                                             NULL);
    if (ret != BK_OK) {
        LOGE("register media ops failed: %d\n", ret);
        goto fail;
    }

    ret = h264e_stream_session_start();
    if (ret != BK_OK) {
        LOGE("h264e stream session start failed: %d\n", ret);
        goto fail;
    }

    s_project_ctx.server_started = 1;
    LOGI("[H264E module] server started, resolution=%ux%u fps=%u (encode not started yet)\n",
         s_project_ctx.width, s_project_ctx.height, s_project_ctx.fps);
    h264e_stream_project_log_pc_h264e_connect_info();
    return BK_OK;

fail:
    h264e_stream_project_server_stop();
    return ret;
}

bk_err_t h264e_stream_project_module_init(uint16_t width, uint16_t height, uint16_t fps)
{
    bk_err_t ret;
    h264e_stream_session_config_t demo_config = {
        .service = H264E_STREAM_SESSION_SERVICE_TCP,
        .auto_force_idr = 1,
    };

    if (s_project_ctx.server_started) {
        return BK_OK;
    }

    ret = h264e_stream_project_set_video_profile(width, height, fps);
    if (ret != BK_OK) {
        return ret;
    }
    s_project_ctx.bitrate_kbps = 1200;

    ret = h264e_stream_session_init_local(&demo_config);
    if (ret != BK_OK) {
        LOGE("h264e module init failed: %d\n", ret);
        return ret;
    }

    ret = h264e_stream_session_register_media_ops(h264e_stream_project_encode_prepare_cb,
                                             h264e_stream_project_encode_stop_cb,
                                             NULL);
    if (ret != BK_OK) {
        LOGE("register media ops failed: %d\n", ret);
        h264e_stream_session_deinit_local();
        return ret;
    }

    s_project_ctx.server_started = 1;
    LOGI("[H264E module] ready, resolution=%ux%u fps=%u\n",
         s_project_ctx.width, s_project_ctx.height, s_project_ctx.fps);
    return BK_OK;
}

bk_err_t h264e_stream_project_module_stop(void)
{
    h264e_stream_project_encode_stop();
    h264e_stream_session_deinit_local();
    s_project_ctx.server_started = 0;
    LOGI("[H264E module] stopped\n");
    return BK_OK;
}

bk_err_t h264e_stream_project_server_stop(void)
{
    h264e_stream_project_encode_stop();
    h264e_stream_session_stop();
    h264e_stream_session_deinit();

    s_project_ctx.server_started = 0;
    LOGI("[H264E module] server stopped\n");
    return BK_OK;
}

bk_err_t h264e_stream_project_start(uint16_t width, uint16_t height, uint16_t fps)
{
    return h264e_stream_project_server_start(width, height, fps);
}

bk_err_t h264e_stream_project_stop(void)
{
    return h264e_stream_project_server_stop();
}

static void cli_h264e_stream_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint16_t width = s_project_ctx.width;
    uint16_t height = s_project_ctx.height;
    uint16_t fps = s_project_ctx.fps;
    bk_err_t ret;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2) {
        LOGI("=== H264E module (record / rate ctrl) ===\n");
        LOGI("  h264e_stream server start [720p|1080p] [fps]\n");
        LOGI("  h264e_stream encode start | encode stop\n");
        LOGI("  h264e_stream server stop  (or: h264e_stream stop)\n");
        LOGI("=== ISP module (capture / image stream) - separate ===\n");
        LOGI("  isp_frame server start [720p|1080p] [fps]\n");
        LOGI("  isp_frame server stop\n");
        LOGI("  (optional legacy tuning: isp tuning start -> TCP 8080)\n");
        return;
    }

    if (os_strcmp(argv[1], "stop") == 0) {
        h264e_stream_project_stop();
        return;
    }

    if (os_strcmp(argv[1], "server") == 0) {
        if (argc < 3) {
            LOGI("usage: h264e_stream server start [720p|1080p] [fps] | server stop\n");
            return;
        }
        if (os_strcmp(argv[2], "stop") == 0) {
            h264e_stream_project_server_stop();
            return;
        }
        if (os_strcmp(argv[2], "start") != 0) {
            LOGI("usage: h264e_stream server start [720p|1080p] [fps] | server stop\n");
            return;
        }
        h264e_stream_project_parse_video_args(argc, argv, 3, &width, &height, &fps);
        ret = h264e_stream_project_server_start(width, height, fps);
        if (ret != BK_OK) {
            LOGE("server start failed: %d\n", ret);
        }
        return;
    }

    if (os_strcmp(argv[1], "encode") == 0) {
        if (argc < 3) {
            LOGI("usage: h264e_stream encode start | encode stop\n");
            return;
        }
        if (os_strcmp(argv[2], "start") == 0) {
            ret = h264e_stream_project_encode_start();
            if (ret != BK_OK) {
                LOGE("encode start failed: %d\n", ret);
            }
            return;
        }
        if (os_strcmp(argv[2], "stop") == 0) {
            ret = h264e_stream_project_encode_stop();
            if (ret != BK_OK) {
                LOGE("encode stop failed: %d\n", ret);
            }
            return;
        }
        LOGI("usage: h264e_stream encode start | encode stop\n");
        return;
    }

    if (os_strcmp(argv[1], "start") == 0) {
        h264e_stream_project_parse_video_args(argc, argv, 2, &width, &height, &fps);
        ret = h264e_stream_project_server_start(width, height, fps);
        if (ret != BK_OK) {
            LOGE("server start failed: %d\n", ret);
        }
        return;
    }

    LOGI("unknown subcommand, run: ap_cmd h264e_stream\n");
}

static void cli_h264e_stream_rc_schema_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;
    (void)argc;
    (void)argv;

    h264e_stream_project_log_json_chunks("VCEncRateCtrl/rateCtrl schema json:",
                                       h264e_stream_session_get_rate_ctrl_schema_json());
}

static void cli_h264e_stream_config_schema_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;
    (void)argc;
    (void)argv;

    h264e_stream_project_log_json_chunks("dynamic form config schema json:",
                                       h264e_stream_session_get_form_json());
}

static void cli_h264e_stream_config_get_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    bk_h264_encode_rate_ctrl_t rate_ctrl = {0};
    bk_h264_encode_ctlr_handle_t enc_handle = s_project_ctx.enc_handle;
    uint32_t gop_frame_count = s_project_ctx.gop_frame_count;
    avdk_err_t ret;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;
    (void)argc;
    (void)argv;

    if (enc_handle == NULL) {
        enc_handle = (bk_h264_encode_ctlr_handle_t)app_h264_encode_handle_get();
    }

    LOGI("config values:\n");
    LOGI("  mode=tcp\n");
    LOGI("  video.resolution=%ux%u\n", s_project_ctx.width, s_project_ctx.height);
    LOGI("  video.width=%u\n", s_project_ctx.width);
    LOGI("  video.height=%u\n", s_project_ctx.height);
    LOGI("  video.fps=%u\n", s_project_ctx.fps);
    LOGI("  video.bitrateKbps=%u\n", s_project_ctx.bitrate_kbps);
    LOGI("  video.gopFrameCount=%u\n", s_project_ctx.gop_frame_count);
    LOGI("  ctrl.forceIdr=true\n");

    if (enc_handle == NULL) {
        LOGI("  rateCtrl=not_started, encoder must be opened before reading live values\n");
        LOGI("  start first: ap_cmd h264e_stream start [720p|1080p] [fps]\n");
        return;
    }

    ret = bk_h264_encode_get_rate_ctrl(enc_handle, &rate_ctrl);
    if (ret != AVDK_ERR_OK) {
        LOGE("get rate ctrl failed: %d\n", ret);
        return;
    }

    LOGI("  rateCtrl.bitrate=%u\n", rate_ctrl.bitrate);
    LOGI("  rateCtrl.qpMinI=%u\n", rate_ctrl.qp_min_i);
    LOGI("  rateCtrl.qpMaxI=%u\n", rate_ctrl.qp_max_i);
    LOGI("  rateCtrl.qpMinP=%u\n", rate_ctrl.qp_min_p);
    LOGI("  rateCtrl.qpMaxP=%u\n", rate_ctrl.qp_max_p);
    LOGI("  vcencRateCtrl.mapped:\n");
    h264e_stream_project_log_mapped_vcenc_rate_ctrl(&rate_ctrl);

    ret = bk_h264_encode_get_gop_frame_count(enc_handle, &gop_frame_count);
    if (ret == AVDK_ERR_OK) {
        LOGI("  live.gopFrameCount=%u\n", gop_frame_count);
    }
}

static void cli_h264e_stream_rc_get_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    bk_h264_encode_rate_ctrl_t rate_ctrl = {0};
    bk_h264_encode_ctlr_handle_t enc_handle = s_project_ctx.enc_handle;
    avdk_err_t ret;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;
    (void)argc;
    (void)argv;

    if (enc_handle == NULL) {
        enc_handle = (bk_h264_encode_ctlr_handle_t)app_h264_encode_handle_get();
    }

    if (enc_handle == NULL) {
        LOGE("encoder is not started\n");
        return;
    }

    ret = bk_h264_encode_get_rate_ctrl(enc_handle, &rate_ctrl);
    if (ret != AVDK_ERR_OK) {
        LOGE("get rate ctrl failed: %d\n", ret);
        return;
    }

    LOGI("VCEncRateCtr/rateCtrl values:\n");
    LOGI("  bitrate=%u\n", rate_ctrl.bitrate);
    LOGI("  qpMinI=%u\n", rate_ctrl.qp_min_i);
    LOGI("  qpMaxI=%u\n", rate_ctrl.qp_max_i);
    LOGI("  qpMinP=%u\n", rate_ctrl.qp_min_p);
    LOGI("  qpMaxP=%u\n", rate_ctrl.qp_max_p);
    h264e_stream_project_log_mapped_vcenc_rate_ctrl(&rate_ctrl);
}

static const struct cli_command s_h264e_stream_commands[] =
{
    {"h264e_stream", "H264E: server/encode start|stop (see ap_cmd h264e_stream)", cli_h264e_stream_cmd},
    {"h264e_stream_config_schema", "show dynamic form config schema summary", cli_h264e_stream_config_schema_cmd},
    {"h264e_stream_config_get", "show dynamic form current values summary", cli_h264e_stream_config_get_cmd},
    {"h264e_stream_rc_schema", "show VCEncRateCtr/rateCtrl field schema", cli_h264e_stream_rc_schema_cmd},
    {"h264e_stream_rc_get", "show VCEncRateCtr/rateCtrl current key-value", cli_h264e_stream_rc_get_cmd},
};

int h264e_stream_project_cli_init(void)
{
    return cli_register_commands(s_h264e_stream_commands,
                                 sizeof(s_h264e_stream_commands) / sizeof(s_h264e_stream_commands[0]));
}
