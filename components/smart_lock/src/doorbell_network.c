#include <common/bk_include.h>
#include "cli.h"
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/int.h>
#include <common/bk_err.h>

#include <os/os.h>
#include <common/bk_kernel_err.h>
#include <string.h>

#include <common/sys_config.h>
#include <components/log.h>
#include <components/event.h>
#include <components/netif.h>
#include <string.h>


#include "doorbell_comm.h"
#include "doorbell_network.h"
#include "doorbell_cmd.h"
#include "doorbell_boarding.h"

#include "wifi_api.h"
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
#include "network_transfer.h"
#endif

#if (CONFIG_EASY_FLASH && CONFIG_EASY_FLASH_V4)
#include "bk_ef.h"
#endif

#define TAG "db-net"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define SOFTAP_DEF_NET_IP        "192.168.10.1"
#define SOFTAP_DEF_NET_MASK      "255.255.255.0"
#define SOFTAP_DEF_NET_GW        "192.168.10.1"
#define SOFTAP_DEF_CHANNEL       1

static void doorbell_wifi_event_cb(void *new_evt)
{
    wifi_linkstate_reason_t info = *((wifi_linkstate_reason_t *)new_evt);
    doorbell_msg_t msg;
    msg.param = info.state;

    switch (info.state)
    {
        case WIFI_LINKSTATE_STA_GOT_IP:
        {
            LOGD("WIFI_LINKSTATE_STA_GOT_IP\r\n");

            msg.event = DBEVT_WIFI_STATION_CONNECTED;
            doorbell_send_msg(&msg);
        }
        break;

        case WIFI_LINKSTATE_STA_DISCONNECTED:
        {
            LOGD("WIFI_LINKSTATE_STA_DISCONNECTED\r\n");

            msg.event = DBEVT_WIFI_STATION_DISCONNECTED;
            doorbell_send_msg(&msg);
        }
        break;

        case WIFI_LINKSTATE_AP_CONNECTED:
        {
            LOGD("WIFI_LINKSTATE_AP_CONNECTED\r\n");
        }
        break;

        case WIFI_LINKSTATE_AP_DISCONNECTED:
        {
            LOGD("WIFI_LINKSTATE_AP_DISCONNECTED\r\n");
        }
        break;

        default:
            LOGD("WIFI_LINKSTATE %d\r\n", info.state);
            break;

    }
}

int doorbell_wifi_sta_connect(char *ssid, char *key)
{
    int len;

    bk_wlan_status_register_cb(doorbell_wifi_event_cb);

    wifi_sta_config_t sta_config = {0};

    len = os_strlen(key);

    if (32 < len)
    {
        LOGE("ssid name more than 32 Bytes\r\n");
        return BK_FAIL;
    }

    os_strcpy(sta_config.ssid, ssid);

    len = os_strlen(key);

    if (64 < len)
    {
        LOGE("key more than 64 Bytes\r\n");
        return BK_FAIL;
    }

    os_strcpy(sta_config.password, key);

    LOGE("ssid:%s key:%s\r\n", sta_config.ssid, sta_config.password);
    BK_LOG_ON_ERR(bk_wifi_sta_set_config(&sta_config));
    BK_LOG_ON_ERR(bk_wifi_sta_start());

    return BK_OK;
}

int doorbell_wifi_soft_ap_start(char *ssid, char *key, uint16_t channel)
{
#if 0
    wifi_ap_config_t ap_config = WIFI_DEFAULT_AP_CONFIG();
    netif_ip4_config_t ip4_config = {0};

    strncpy(ip4_config.ip, SOFTAP_DEF_NET_IP, NETIF_IP4_STR_LEN);
    strncpy(ip4_config.mask, SOFTAP_DEF_NET_MASK, NETIF_IP4_STR_LEN);
    strncpy(ip4_config.gateway, SOFTAP_DEF_NET_GW, NETIF_IP4_STR_LEN);
    strncpy(ip4_config.dns, SOFTAP_DEF_NET_GW, NETIF_IP4_STR_LEN);
    BK_LOG_ON_ERR(bk_netif_set_ip4_config(NETIF_IF_AP, &ip4_config));

    os_memset(ap_config.ssid, 0, sizeof(ap_config.ssid));
    os_memset(ap_config.password, 0, sizeof(ap_config.password));

    strncpy(ap_config.ssid, ssid, strlen(ssid));

    if ((key != NULL) && (strlen(key) > 0))
    {
        strncpy(ap_config.password, key, strlen(key));
    }

    ap_config.channel = channel;

    bk_wlan_status_register_cb(doorbell_wifi_event_cb);

    BK_LOGI(TAG, "ssid:%s  key:%s\r\n", ap_config.ssid, ap_config.password);
    BK_LOG_ON_ERR(bk_wifi_ap_set_config(&ap_config));

    return bk_wifi_ap_start();
#else
    LOGW("%s, %d, #################warning: current not adapt###############\n", __func__, __LINE__);
    return BK_FAIL;
#endif
}


bk_err_t doorbell_save_wifi_info_to_flash(doorbell_boarding_info_t *boarding_info)
{
    db_wifi_connect_info_t wifi_info;

    if (boarding_info == NULL || 
        boarding_info->boarding_info.ssid_value == NULL ||
        boarding_info->boarding_info.password_value == NULL)
    {
        LOGE("Invalid boarding_info or WiFi info is NULL\n");
        return BK_FAIL;
    }

    os_memset(&wifi_info, 0, sizeof(db_wifi_connect_info_t));

    uint16_t ssid_len = boarding_info->boarding_info.ssid_length;
    if (ssid_len > sizeof(wifi_info.db_ssid) - 1)
    {
        ssid_len = sizeof(wifi_info.db_ssid) - 1;
    }
    os_memcpy(wifi_info.db_ssid, boarding_info->boarding_info.ssid_value, ssid_len);
    wifi_info.db_ssid[ssid_len] = '\0';

    uint16_t pwd_len = boarding_info->boarding_info.password_length;
    if (pwd_len > sizeof(wifi_info.db_pwd) - 1)
    {
        pwd_len = sizeof(wifi_info.db_pwd) - 1;
    }
    os_memcpy(wifi_info.db_pwd, boarding_info->boarding_info.password_value, pwd_len);
    wifi_info.db_pwd[pwd_len] = '\0';

    bk_set_env_enhance("db_wifi_info", &wifi_info, sizeof(db_wifi_connect_info_t));
    
    LOGI("WiFi info saved to flash: SSID=%s\n", wifi_info.db_ssid);
    
    return BK_OK;
}

bk_err_t doorbell_get_wifi_info_from_flash(db_wifi_connect_info_t *wifi_info)
{
    bk_err_t ret = BK_OK;

    ret = bk_get_env_enhance("db_wifi_info", wifi_info, sizeof(db_wifi_connect_info_t));
    if (ret <= 0)
    {
        LOGE("Failed to get WiFi info from flash\n");
        return BK_FAIL;
    }

    return ret;
}

bk_err_t doorbell_del_wifi_info_from_flash(void)
{
    bk_err_t ret = bk_set_env_enhance("db_wifi_info", NULL, 0);
    if (ret != BK_OK)
    {
        LOGE("Failed to del wifi info from flash\n");
        return BK_FAIL;
    }
    LOGI("Wifi info deleted from flash\n");
    return BK_OK;
}

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
bk_err_t doorbell_save_server_net_info_to_flash(uint8_t *data)
{
     ntwk_server_net_info_t *net_info = (ntwk_server_net_info_t *)data;

    if (net_info == NULL)
    {
        LOGE("Invalid net_info parameter\n");
        return BK_FAIL;
    }

    LOGI("Received Server net info: IP=%s, Cmd Port=%s, Video Port=%s, Audio Port=%s\n",
        net_info->ip_addr,
        net_info->cmd_port,
        net_info->video_port,
        net_info->audio_port);

    bk_err_t ret = bk_set_env_enhance("db_server_net_info", net_info, sizeof(ntwk_server_net_info_t));
    if (ret != BK_OK)
    {
        LOGE("Failed to save server net info to flash\n");
        return BK_FAIL;
    }

    ret = ntwk_trans_set_server_net_info(net_info);
    if (ret != BK_OK)
    {
        LOGE("Failed to set server net info to network transfer module\n");
        return BK_FAIL;
    }

    LOGI("Server net info saved to flash: IP=%s, Cmd Port=%s, Video Port=%s, Audio Port=%s\n",
         net_info->ip_addr,
         net_info->cmd_port,
         net_info->video_port,
         net_info->audio_port);

    return BK_OK;
}

bk_err_t doorbell_get_server_net_info_from_flash(ntwk_server_net_info_t *net_info)
{
    if (net_info == NULL)
    {
        LOGE("Invalid net_info parameter\n");
        return BK_FAIL;
    }

    bk_err_t ret = bk_get_env_enhance("db_server_net_info", net_info, sizeof(ntwk_server_net_info_t));
    if (ret <= 0)
    {
        LOGE("Failed to get server net info from flash\n");
        return BK_FAIL;
    }

    LOGI("Server net info loaded from flash: IP=%s, Cmd Port=%s, Video Port=%s, Audio Port=%s\n",
         net_info->ip_addr,
         net_info->cmd_port,
         net_info->video_port,
         net_info->audio_port);

    return BK_OK;
}

bk_err_t doorbell_del_server_net_info_from_flash(void)
{
    bk_err_t ret = bk_set_env_enhance("db_server_net_info", NULL, 0);
    if (ret != BK_OK)
    {
        LOGE("Failed to del server net info from flash\n");
        return BK_FAIL;
    }
    LOGI("Server net info deleted from flash\n");
    return BK_OK;
}

#endif

bk_err_t doorbell_save_ntwk_service_info_to_flash(db_ntwk_service_info_t *service_info)
{
    if (service_info == NULL)
    {
        LOGE("Invalid service_info parameter\n");
        return BK_FAIL;
    }

    bk_err_t ret = bk_set_env_enhance("db_ntwk_service_info", service_info, sizeof(db_ntwk_service_info_t));
    if (ret != BK_OK)
    {
        LOGE("Failed to save network service info to flash\n");
        return BK_FAIL;
    }

    LOGI("Network service info saved to flash: service=%d\n", service_info->db_service);

    return BK_OK;
}

bk_err_t doorbell_get_ntwk_service_info_from_flash(db_ntwk_service_info_t *service_info)
{
    if (service_info == NULL)
    {
        LOGE("Invalid service_info parameter\n");
        return BK_FAIL;
    }

    bk_err_t ret = bk_get_env_enhance("db_ntwk_service_info", service_info, sizeof(db_ntwk_service_info_t));
    if (ret <= 0)
    {
        LOGE("Failed to get network service info from flash\n");
        return BK_FAIL;
    }

    LOGI("Network service info loaded from flash: service=%d\n", service_info->db_service);

    return BK_OK;
}

bk_err_t doorbell_del_ntwk_service_info_from_flash(void)
{
    bk_err_t ret = bk_set_env_enhance("db_ntwk_service_info", NULL, 0);
    if (ret != BK_OK)
    {
        LOGE("Failed to del ntwk service info from flash\n");
        return BK_FAIL;
    }
    LOGI("Network service info deleted from flash\n");
    return BK_OK;
}

bk_err_t doorbell_save_keepalive_interval_to_flash(uint32_t interval_ms)
{
    bk_err_t ret = bk_set_env_enhance("doorbell_keepalive_interval", &interval_ms, sizeof(interval_ms));
    if (ret != BK_OK)
    {
        LOGE("Failed to save keepalive interval to flash\n");
        return BK_FAIL;
    }
    LOGI("Keepalive interval saved to flash: %u ms\n", interval_ms);
    return BK_OK;
}

bk_err_t doorbell_get_keepalive_interval_from_flash(uint32_t *interval_ms)
{
    if (interval_ms == NULL)
    {
        return BK_FAIL;
    }

    bk_err_t ret = bk_get_env_enhance("doorbell_keepalive_interval", interval_ms, sizeof(uint32_t));
    if (ret <= 0)
    {
        LOGE("Failed to get keepalive interval from flash\n");
        return BK_FAIL;
    }
    LOGI("Keepalive interval loaded from flash: %u ms\n", *interval_ms);
    return BK_OK;
}

bk_err_t doorbell_del_keepalive_interval_from_flash(void)
{
    bk_err_t ret = bk_set_env_enhance("doorbell_keepalive_interval", NULL, 0);
    if (ret != BK_OK)
    {
        LOGE("Failed to del keepalive interval from flash\n");
        return BK_FAIL;
    }
    LOGI("Keepalive interval deleted from flash\n");
    return BK_OK;
}

bk_err_t doorbell_del_all_info_from_flash(void)
{
    bk_err_t ret = BK_OK;
    ret = doorbell_del_wifi_info_from_flash();
    if (ret != BK_OK) {
        LOGE("Failed to del wifi info from flash\n");
        return BK_FAIL;
    }
    ret = doorbell_del_ntwk_service_info_from_flash();
    if (ret != BK_OK) {
        LOGE("Failed to del ntwk service info from flash\n");
        return BK_FAIL;
    }
    ret = doorbell_del_keepalive_interval_from_flash();
    if (ret != BK_OK) {
        LOGE("Failed to del keepalive interval from flash\n");
        return BK_FAIL;
    }
    #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    ret = doorbell_del_server_net_info_from_flash();
    if (ret != BK_OK) {
            LOGE("Failed to del server net info from flash\n");
            return BK_FAIL;
    }
    #endif

    return BK_OK;
}
