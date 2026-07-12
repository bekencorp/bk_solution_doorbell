#include "bk_private/bk_init.h"
#include <components/system.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <modules/wifi.h>
#include <modules/wifi_types.h>
#include <os/str.h>
#include "cli.h"
#include "media_service.h"
#include "h264e_stream_project.h"
#include "isp_frame_project.h"
#include "media_preview_server.h"

#define TAG "media_preview_main"
#include "devices_mgmt.h"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static char s_sta_ssid[WIFI_SSID_STR_LEN];
static char s_sta_password[WIFI_PASSWORD_LEN];

static bk_err_t wifi_sta_connect(void)
{
    wifi_sta_config_t sta_config = {0};

    os_strncpy(sta_config.ssid, s_sta_ssid, WIFI_SSID_STR_LEN - 1);
    os_strncpy(sta_config.password, s_sta_password, WIFI_PASSWORD_LEN - 1);

    BK_RETURN_ON_ERR(bk_wifi_sta_set_config(&sta_config));
    BK_RETURN_ON_ERR(bk_wifi_sta_start());
    return BK_OK;
}

static void cli_wifi_sta_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 4)
    {
        LOGI("usage:\n");
        LOGI("  wifi_sta connect [ssid] [password]\n");
        return;
    }

    if (os_strcmp(argv[1], "connect") != 0)
    {
        LOGE("unknown subcmd: %s\n", argv[1]);
        return;
    }

    os_strncpy(s_sta_ssid, argv[2], WIFI_SSID_STR_LEN - 1);
    s_sta_ssid[WIFI_SSID_STR_LEN - 1] = '\0';

    os_strncpy(s_sta_password, argv[3], WIFI_PASSWORD_LEN - 1);
    s_sta_password[WIFI_PASSWORD_LEN - 1] = '\0';

    if (s_sta_ssid[0] == '\0')
    {
        LOGE("ssid is empty, use: wifi_sta connect <ssid> [password]\n");
        return;
    }

    if (wifi_sta_connect() != BK_OK)
    {
        LOGE("wifi_sta connect failed\n");
    }
}

static const struct cli_command s_wifi_sta_commands[] =
{
    {"wifi_sta", "Wi-Fi STA: connect <ssid> [password]", cli_wifi_sta_cmd},
};

static int wifi_cli_init(void)
{
    return cli_register_commands(s_wifi_sta_commands,
                                 sizeof(s_wifi_sta_commands) / sizeof(s_wifi_sta_commands[0]));
}

int main(void)
{
    bk_init();
    media_service_init();
    devices_mgmt_init();

#ifdef CONFIG_FRAME_BUFFER
    bk_frame_buffer_init();
#endif

    if (h264e_stream_project_cli_init() != BK_OK) {
        LOGE("h264e stream cli init failed\n");
    }

    if (isp_frame_project_cli_init() != BK_OK) {
        LOGE("isp frame cli init failed\n");
    }

    if (media_preview_server_cli_init() != BK_OK) {
        LOGE("media preview cli init failed\n");
    }

    if (wifi_cli_init() != 0) {
        LOGE("wifi sta cli init failed\n");
    }

    if (media_preview_server_start(2304, 1296, 20) != BK_OK) {
        LOGE("media preview server auto start failed\n");
    }

    LOGI("media preview example ready.\n");
    LOGI("  Unified server : default 2304x1296 @ 20fps\n");
    LOGI("  PC selects H264E or ISP by JSON-RPC method\n");
    LOGI("  Wi-Fi CLI     : wifi_sta connect <ssid> [password]\n");
    return 0;
}
