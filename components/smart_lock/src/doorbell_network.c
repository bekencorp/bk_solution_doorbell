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

#include "wifi_api.h"

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




