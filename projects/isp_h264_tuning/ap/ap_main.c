#include "bk_private/bk_init.h"
#include <components/system.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <modules/wifi.h>
#include <os/str.h>
#include "media_service.h"
#include "h264e_stream_project.h"
#include "isp_frame_project.h"
#include "media_preview_server.h"

#define TAG "media_preview_main"
#include "devices_mgmt.h"

#define MEDIA_PREVIEW_STA_SSID      "Beken-ACL-2.4G"
#define MEDIA_PREVIEW_STA_PASSWORD  "123412345"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static bk_err_t media_preview_wifi_sta_connect(void)
{
    wifi_sta_config_t sta_config = {0};

    os_strcpy(sta_config.ssid, MEDIA_PREVIEW_STA_SSID);
    os_strcpy(sta_config.password, MEDIA_PREVIEW_STA_PASSWORD);

    LOGI("auto connect STA ssid=%s\n", sta_config.ssid);
    BK_RETURN_ON_ERR(bk_wifi_sta_set_config(&sta_config));
    BK_RETURN_ON_ERR(bk_wifi_sta_start());
    return BK_OK;
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

    if (media_preview_wifi_sta_connect() != BK_OK) {
        LOGE("media preview Wi-Fi STA auto connect failed\n");
    }

    if (media_preview_server_start(2304, 1296, 20) != BK_OK) {
        LOGE("media preview server auto start failed\n");
    }

    LOGI("media preview example ready.\n");
    LOGI("  Unified server : default 2304x1296 @ 20fps\n");
    LOGI("  PC selects H264E or ISP by JSON-RPC method\n");
    return 0;
}
