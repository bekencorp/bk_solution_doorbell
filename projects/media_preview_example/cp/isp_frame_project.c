#include <os/os.h>
#include <os/str.h>
#include <components/log.h>
#include <components/netif.h>
#include "cli.h"
#include "driver/gpio.h"
#include <common/avdk_pixel_types.h>
#include "avdk_error.h"
#include "network_type.h"

#include "app_camera.h"
#include "isp_frame_session.h"
#include "isp_frame_capture_config.h"
#include "isp_frame_priv.h"
#include "isp_frame_project.h"

#define TAG "isp_frame_proj"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

typedef struct
{
    uint8_t server_started;
    uint16_t fps;
    uint16_t width;
    uint16_t height;
} isp_frame_project_ctx_t;

static isp_frame_project_ctx_t s_isp_project_ctx = {
    .fps = 25,
    .width = 1280,
    .height = 720,
};

static void isp_frame_project_fill_camera_config(camera_board_config_t *config,
                                                const isp_frame_capture_config_t *cap,
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

    config->mipi.sensor_max_width = cap->sensor_w;
    config->mipi.sensor_max_height = cap->sensor_h;
    config->isp.mp_width = cap->isp_w;
    config->isp.mp_height = cap->isp_h;

    config->mipi.sensor_fps = fps ? fps : 25;
    config->isp.mp_enable = true;
    /* frame mode (flexa off): required for app_isp_camera_channel_read used by preview */
    config->isp.mp_flexa = false;
    config->isp.mp_format = isp_frame_format_to_bk_pixel(cap->format);
    config->isp.sp_enable = false;
    config->isp.sp_flexa = false;
}

static bk_err_t isp_frame_project_validate_resolution(uint16_t width, uint16_t height)
{
    if ((width == 1280 && height == 720) ||
        (width == 1920 && height == 1080)) {
        return BK_OK;
    }

    LOGE("unsupported resolution %ux%u (supported: 1280x720, 1920x1080)\n", width, height);
    return BK_ERR_PARAM;
}

static void isp_frame_project_parse_video_args(int argc, char **argv, int start_idx,
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
                *fps = 25;
            }
        }
    }
}

static bk_err_t isp_frame_project_set_video_profile(uint16_t width, uint16_t height, uint16_t fps)
{
    bk_err_t ret = isp_frame_project_validate_resolution(width, height);
    if (ret != BK_OK) {
        return ret;
    }

    s_isp_project_ctx.width = width;
    s_isp_project_ctx.height = height;
    s_isp_project_ctx.fps = fps ? fps : 25;
    return BK_OK;
}

static int isp_frame_project_frame_read(uint8_t *frame, uint32_t size, uint32_t timeout_ms)
{
    const isp_frame_capture_config_t *cap = isp_frame_session_get_capture_config();
    uint32_t read_timeout = timeout_ms ? timeout_ms : 100;

    if (timeout_ms == 0 && cap != NULL && cap->rx_timeout > 0)
    {
        read_timeout = isp_frame_rx_timeout_ms_for_read(cap->rx_timeout, read_timeout);
    }

    int ret = app_isp_camera_channel_read(APP_ISP_MP_CHN_ID, frame, size, read_timeout);

    return (ret == AVDK_ERR_OK) ? 0 : -1;
}

static bk_err_t isp_frame_project_media_start(void *user_data)
{
    camera_board_config_t config;
    const isp_frame_capture_config_t *cap = isp_frame_session_get_capture_config();
    avdk_err_t ret;

    (void)user_data;

    if (cap == NULL)
    {
        return BK_FAIL;
    }

    (void)app_isp_camera_turn_off();

    /* Use capture config from setParameterValues / isp.start (do not override with CLI profile). */
    isp_frame_project_fill_camera_config(&config, cap, s_isp_project_ctx.fps);
    app_camera_board_config_set(&config);
    ret = app_isp_mipi_camera_turn_on(app_camera_board_config_get());
    if (ret != AVDK_ERR_OK) {
        LOGE("isp camera turn on failed: %d\n", ret);
        return BK_FAIL;
    }

    LOGI("[ISP module] camera started sensor=%ux%u isp=%ux%u format=%u pattern=%s\n",
         cap->sensor_w, cap->sensor_h, cap->isp_w, cap->isp_h, cap->format, cap->pattern);
    return BK_OK;
}

static bk_err_t isp_frame_project_media_stop(void *user_data)
{
    avdk_err_t ret;

    (void)user_data;
    ret = app_isp_camera_turn_off();
    if (ret != AVDK_ERR_OK) {
        LOGE("isp camera turn off failed: %d\n", ret);
        return BK_FAIL;
    }

    LOGI("[ISP module] camera stopped\n");
    return BK_OK;
}

bk_err_t isp_frame_project_server_start(uint16_t width, uint16_t height, uint16_t fps)
{
    bk_err_t ret;
    isp_frame_session_config_t demo_config = {
        .service = ISP_FRAME_SESSION_SERVICE_TCP,
        .auto_force_idr = 1,
    };

    if (s_isp_project_ctx.server_started) {
        LOGI("isp_frame server already started\n");
        return BK_OK;
    }

    ret = isp_frame_project_set_video_profile(width, height, fps);
    if (ret != BK_OK) {
        return ret;
    }
    isp_frame_capture_apply_resolution(s_isp_project_ctx.width, s_isp_project_ctx.height,
                                      s_isp_project_ctx.width, s_isp_project_ctx.height);

    ret = isp_frame_session_init(&demo_config);
    if (ret != BK_OK) {
        LOGE("isp wifi demo init failed: %d\n", ret);
        goto fail;
    }

    ret = isp_frame_session_register_media_ops(isp_frame_project_media_start,
                                           isp_frame_project_media_stop,
                                           NULL);
    if (ret != BK_OK) {
        LOGE("register media ops failed: %d\n", ret);
        goto fail;
    }

    isp_frame_stream_register_read_cb(isp_frame_project_frame_read);

    ret = isp_frame_session_start();
    if (ret != BK_OK) {
        LOGE("isp wifi demo start failed: %d\n", ret);
        goto fail;
    }

    s_isp_project_ctx.server_started = 1;
    LOGI("[ISP module] server started, resolution=%ux%u fps=%u\n",
         s_isp_project_ctx.width, s_isp_project_ctx.height, s_isp_project_ctx.fps);
    return BK_OK;

fail:
    isp_frame_project_server_stop();
    return ret;
}

bk_err_t isp_frame_project_module_init(uint16_t width, uint16_t height, uint16_t fps)
{
    bk_err_t ret;
    isp_frame_session_config_t demo_config = {
        .service = ISP_FRAME_SESSION_SERVICE_TCP,
        .auto_force_idr = 1,
    };

    if (s_isp_project_ctx.server_started) {
        return BK_OK;
    }

    ret = isp_frame_project_set_video_profile(width, height, fps);
    if (ret != BK_OK) {
        return ret;
    }
    isp_frame_capture_apply_resolution(s_isp_project_ctx.width, s_isp_project_ctx.height,
                                      s_isp_project_ctx.width, s_isp_project_ctx.height);

    ret = isp_frame_session_init_local(&demo_config);
    if (ret != BK_OK) {
        LOGE("isp wifi module init failed: %d\n", ret);
        return ret;
    }

    ret = isp_frame_session_register_media_ops(isp_frame_project_media_start,
                                           isp_frame_project_media_stop,
                                           NULL);
    if (ret != BK_OK) {
        LOGE("register media ops failed: %d\n", ret);
        isp_frame_session_deinit_local();
        return ret;
    }

    isp_frame_stream_register_read_cb(isp_frame_project_frame_read);

    s_isp_project_ctx.server_started = 1;
    LOGI("[ISP module] ready, resolution=%ux%u fps=%u\n",
         s_isp_project_ctx.width, s_isp_project_ctx.height, s_isp_project_ctx.fps);
    return BK_OK;
}

bk_err_t isp_frame_project_module_stop(void)
{
    isp_frame_session_deinit_local();
    s_isp_project_ctx.server_started = 0;
    LOGI("[ISP module] stopped\n");
    return BK_OK;
}

bk_err_t isp_frame_project_server_stop(void)
{
    if (app_isp_handle_get() != NULL) {
        (void)app_isp_camera_turn_off();
    }
    isp_frame_session_stop();
    isp_frame_session_deinit();
    s_isp_project_ctx.server_started = 0;
    LOGI("[ISP module] server stopped\n");
    return BK_OK;
}

static void cli_isp_frame_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint16_t width = s_isp_project_ctx.width;
    uint16_t height = s_isp_project_ctx.height;
    uint16_t fps = s_isp_project_ctx.fps;
    bk_err_t ret;

    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2) {
        LOGI("=== ISP module (capture / image stream) ===\n");
        LOGI("  isp_frame server start [720p|1080p] [fps]\n");
        LOGI("  isp_frame server stop\n");
        LOGI("  PC: start -> preview (1 frame) -> captureFrames (N frames) on TCP %u\n",
             NTWK_TRANS_TCP_VIDEO_PORT);
        LOGI("=== H264E module (separate) ===\n");
        LOGI("  h264e_stream server start ...\n");
        return;
    }

    if (os_strcmp(argv[1], "server") == 0) {
        if (argc < 3) {
            LOGI("usage: isp_frame server start [720p|1080p] [fps] | server stop\n");
            return;
        }
        if (os_strcmp(argv[2], "stop") == 0) {
            isp_frame_project_server_stop();
            return;
        }
        if (os_strcmp(argv[2], "start") != 0) {
            LOGI("usage: isp_frame server start [720p|1080p] [fps] | server stop\n");
            return;
        }
        isp_frame_project_parse_video_args(argc, argv, 3, &width, &height, &fps);
        ret = isp_frame_project_server_start(width, height, fps);
        if (ret != BK_OK) {
            LOGE("isp_frame server start failed: %d\n", ret);
        }
        return;
    }

    LOGI("unknown subcommand, run: ap_cmd isp_frame\n");
}

static const struct cli_command s_isp_frame_commands[] =
{
    {"isp_frame", "ISP: server start|stop (JSON + image stream)", cli_isp_frame_cmd},
};

int isp_frame_project_cli_init(void)
{
    return cli_register_commands(s_isp_frame_commands,
                                 sizeof(s_isp_frame_commands) / sizeof(s_isp_frame_commands[0]));
}
