#include "bk_private/bk_init.h"
#include <os/os.h>
#include <string.h>

#include "doorbell_network_transfer.h"
#include "network_transfer.h"
#include "doorbell_cmd.h"
#include "doorbell_audio_device.h"
#include "doorbell_comm.h"
#include "doorbell_network.h"
//#include "keepalive/db_keepalive.h"

#define TAG "db-ntwk"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)


#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
static uint8_t chan_connected_mask = 0;

#define CHAN_CTRL_BIT    (1 << NTWK_TRANS_CHAN_CTRL)
#define CHAN_VIDEO_BIT   (1 << NTWK_TRANS_CHAN_VIDEO)
#define CHAN_AUDIO_BIT   (1 << NTWK_TRANS_CHAN_AUDIO)
#define ALL_CHAN_MASK    (CHAN_CTRL_BIT | CHAN_VIDEO_BIT | CHAN_AUDIO_BIT)

static void reset_chan_connected_mask(void)
{
    chan_connected_mask = 0;
}

static bool check_all_channels_connected(void)
{
    return (chan_connected_mask & ALL_CHAN_MASK) == ALL_CHAN_MASK;
}

static void set_chan_connected(chan_type_t chan_type)
{
    chan_connected_mask |= (1 << chan_type);
}

static void clear_chan_connected(chan_type_t chan_type)
{
    chan_connected_mask &= ~(1 << chan_type);
}
#endif

bk_err_t doorbell_bk_net_cntrl_recv(uint8_t *data, uint32_t length)
{
    LOGI("%s: length=%d\n", __func__, length);
    doorbell_transmission_cmd_recive_callback(data, length);
    return BK_OK;
}

bk_err_t doorbell_bk_net_video_recv(uint8_t *data, uint32_t length)
{
    /* TODO: Implement video receive callback */
    return BK_OK;
}

bk_err_t doorbell_bk_net_audio_recv(uint8_t *data, uint32_t length)
{
    doorbell_audio_data_callback(data, length);
    return BK_OK;
}

static dbevt_t handle_start_event(db_ntwk_type_t ntwk_type, chan_type_t chan_type, int param)
{
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (chan_type == NTWK_TRANS_CHAN_CTRL) {
        reset_chan_connected_mask();
    }
#endif
    switch (chan_type)
    {
        case NTWK_TRANS_CHAN_CTRL:
        {
            #if 1//!CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
            }
            else if (ntwk_type == DB_NTWK_TYPE_UDP) {
            }else if (ntwk_type == DB_NTWK_TYPE_CS2) {
                return DBEVT_P2P_CS2_SERVICE_START_RESPONSE;
            }
            #endif
        } break;
        case NTWK_TRANS_CHAN_VIDEO:
        {
            #if 1//!CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
                return DBEVT_LAN_TCP_SERVICE_START_RESPONSE;
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
                return DBEVT_LAN_UDP_SERVICE_START_RESPONSE;
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
            #endif
        } break;
        case NTWK_TRANS_CHAN_AUDIO:
        {
            #if 1//!CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
            #endif
        } break;
        
        default:
            return 0;
    }

    return 0;
}

static dbevt_t handle_connected_event(db_ntwk_type_t ntwk_type, chan_type_t chan_type, int param)
{
    switch (chan_type)
    {
        case NTWK_TRANS_CHAN_CTRL:
        {
            #if !CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
                return DBEVT_REMOTE_DEVICE_CONNECTED;
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
                return DBEVT_REMOTE_DEVICE_CONNECTED;
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
            #else
            set_chan_connected(chan_type);
            if (check_all_channels_connected()) {
                if (ntwk_type == DB_NTWK_TYPE_TCP || ntwk_type == DB_NTWK_TYPE_UDP || ntwk_type == DB_NTWK_TYPE_CS2) {
                    return DBEVT_REMOTE_DEVICE_CONNECTED;
                }
            }
            #endif
        } break;
        case NTWK_TRANS_CHAN_VIDEO:
        {
            #if !CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
            #else
            set_chan_connected(chan_type);
            if (check_all_channels_connected()) {
                if (ntwk_type == DB_NTWK_TYPE_TCP || ntwk_type == DB_NTWK_TYPE_UDP || ntwk_type == DB_NTWK_TYPE_CS2) {
                    return DBEVT_REMOTE_DEVICE_CONNECTED;
                }
            }
            #endif
        } break;

        case NTWK_TRANS_CHAN_AUDIO:
        {
            #if !CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
            #else
            set_chan_connected(chan_type);
            if (check_all_channels_connected()) {
                if (ntwk_type == DB_NTWK_TYPE_TCP || ntwk_type == DB_NTWK_TYPE_UDP || ntwk_type == DB_NTWK_TYPE_CS2) {
                    return DBEVT_REMOTE_DEVICE_CONNECTED;
                }
            }
            #endif
        } break;

        default:
            return 0;
    }

    return 0;
}

static dbevt_t handle_disconnected_event(db_ntwk_type_t ntwk_type, chan_type_t chan_type, int param)
{
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    clear_chan_connected(chan_type);
#endif

    switch (chan_type)
    {
        case NTWK_TRANS_CHAN_CTRL:
        {
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
                return DBEVT_REMOTE_DEVICE_DISCONNECTED;
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
                return DBEVT_REMOTE_DEVICE_DISCONNECTED;
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
                return DBEVT_REMOTE_DEVICE_DISCONNECTED;
            }
        } break;
        case NTWK_TRANS_CHAN_VIDEO:
        {
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
                return (param == ENOTCONN) ? DBEVT_IMAGE_TCP_SERVICE_DISCONNECTED 
                                            : DBEVT_REMOTE_DEVICE_DISCONNECTED;
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
                return (param != ENOTCONN) ? DBEVT_REMOTE_DEVICE_DISCONNECTED : 0;
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
        } break;
        case NTWK_TRANS_CHAN_AUDIO:
        {
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
                return (param != ENOTCONN) ? DBEVT_REMOTE_DEVICE_DISCONNECTED : 0;
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
                return (param != ENOTCONN) ? DBEVT_REMOTE_DEVICE_DISCONNECTED : 0;
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
        } break;

        default:
            return 0;
    }

    return 0;
}

static dbevt_t handle_stop_event(db_ntwk_type_t ntwk_type, chan_type_t chan_type, int param)
{
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (chan_type == NTWK_TRANS_CHAN_CTRL) {
        reset_chan_connected_mask();
    }
#endif

    switch (chan_type)
    {
        case NTWK_TRANS_CHAN_CTRL:
        {
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
                return (param != ENOTCONN) ? DBEVT_REMOTE_DEVICE_DISCONNECTED : 0;
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
                return (param != ENOTCONN) ? DBEVT_REMOTE_DEVICE_DISCONNECTED : 0;
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {

                return DBEVT_REMOTE_DEVICE_DISCONNECTED;
            }
        } break;
        case NTWK_TRANS_CHAN_VIDEO:
        {
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
        } break;
        case NTWK_TRANS_CHAN_AUDIO:
        {
            if (ntwk_type == DB_NTWK_TYPE_TCP) {
            } else if (ntwk_type == DB_NTWK_TYPE_UDP) {
            } else if (ntwk_type == DB_NTWK_TYPE_CS2) {
            }
        } break;
        default:
            return 0;
    }

    return 0;
}

void doorbell_bk_net_msg_evt_handle(ntwk_trans_event_t *event)
{
    const char *service_name = ntwk_trans_get_service_name();
    db_ntwk_type_t ntwk_type = DB_NTWK_TYPE_NONE;

    if (service_name == NULL)
    {
        LOGE("%s: service_name is NULL\n", __func__);
        return;
    }

    if (event == NULL)
    {
        LOGE("%s: event is NULL\n", __func__);
        return;
    }

    ntwk_type = (strcmp(service_name, "tcp_service") == 0) ? DB_NTWK_TYPE_TCP :
                (strcmp(service_name, "udp_service") == 0) ? DB_NTWK_TYPE_UDP :
                (strcmp(service_name, "cs2_service") == 0) ? DB_NTWK_TYPE_CS2 : DB_NTWK_TYPE_NONE;


    LOGD("%s: event_code=%d, chan_type=%d, service=%s, param=%d\n",
         __func__, event->code, event->chan_type, service_name, event->param);

    dbevt_t event_type = 0;

    switch (event->code)
    {
        case NTWK_TRANS_EVT_START:
            event_type = handle_start_event(ntwk_type, event->chan_type, event->param);
            break;

        case NTWK_TRANS_EVT_CONNECTED:
            event_type = handle_connected_event(ntwk_type, event->chan_type, event->param);
            break;

        case NTWK_TRANS_EVT_DISCONNECTED:
            event_type = handle_disconnected_event(ntwk_type, event->chan_type, event->param);
            break;

        case NTWK_TRANS_EVT_STOP:
            event_type = handle_stop_event(ntwk_type, event->chan_type, event->param);
            break;

        default:
            LOGD("%s: unhandled event code=%d\n", __func__, event->code);
            break;
    }

    if (event_type != 0)
    {
        doorbell_msg_t msg = {0};
        msg.event = event_type;
        msg.param = 0;
        LOGD("%s: sending event=%d\n", __func__, msg.event);
        doorbell_send_msg(&msg);
    }
}

static bk_err_t doorbell_network_transfer_start(char *service_name, void *param)
{
    void *ctrl_param;

    LOGI("%s start\n", __func__);

    //configure message event callback
    ntwk_trans_register_msg_event_cb(doorbell_bk_net_msg_evt_handle);

    //configure ctrl channel
    ntwk_trans_register_ctrl_recv_cb(doorbell_bk_net_cntrl_recv);
    // For cs2_service, pass param; for tcp_service and udp_service, pass NULL
    ctrl_param = (strcmp(service_name, "cs2_service") == 0) ? param : NULL;
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_CTRL, ctrl_param);

    //configure video channel
    ntwk_trans_register_video_recv_cb(doorbell_bk_net_video_recv);
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_VIDEO, NULL);

    //configure audio channel
    ntwk_trans_register_audio_recv_cb(doorbell_bk_net_audio_recv);
    ntwk_trans_chan_start(NTWK_TRANS_CHAN_AUDIO, NULL);

    LOGI("%s end\n", __func__);

    return BK_OK;
}

bk_err_t doorbell_network_transfer_stop(void)
{
    LOGI("%s start\n", __func__);

    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_CTRL);
    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_VIDEO);
    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_AUDIO);

    LOGI("%s end\n", __func__);

    return BK_OK;
}

bk_err_t doorbell_bk_network_transfer_init(char *service_name, void *param)
{
    LOGI("%s start\r\n", __func__);

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    ntwk_server_net_info_t net_info;
    bk_err_t ret;

    ret = doorbell_get_server_net_info_from_flash(&net_info);
    if (ret != BK_OK)
    {
        LOGE("Failed to get server_net_info from flash\n");
        return BK_FAIL;
    }

    ntwk_trans_set_server_net_info(&net_info);
#endif

    if (strcmp(service_name, "tcp_service") == 0)
    {
        bk_tcp_trans_service_init(service_name);
        doorbell_network_transfer_start(service_name, param);
    }
    else if (strcmp(service_name, "udp_service") == 0)
    {
        bk_udp_trans_service_init(service_name);
        doorbell_network_transfer_start(service_name, param);
    }
    else if (strcmp(service_name, "cs2_service") == 0)
    {
        bk_cs2_trans_service_init(service_name);
        doorbell_network_transfer_start(service_name, param);
    }
    else
    {
        LOGE("Invalid service name: %s\n", service_name);
        return BK_FAIL;
    }

    LOGI("%s end\r\n", __func__);

    return BK_OK;
}

bk_err_t doorbell_bk_network_transfer_deinit(char *service_name)
{
    doorbell_network_transfer_stop();

    if (strcmp(service_name, "tcp_service") == 0)
    {
        bk_tcp_trans_service_deinit();
    }
    else if (strcmp(service_name, "udp_service") == 0)
    {
        bk_udp_trans_service_deinit();
    }
    else if (strcmp(service_name, "cs2_service") == 0)
    {
        bk_cs2_trans_service_deinit();
    }
    else
    {
        LOGE("Invalid service name: %s\n", service_name);
        return BK_FAIL;
    }

    return BK_OK;
}