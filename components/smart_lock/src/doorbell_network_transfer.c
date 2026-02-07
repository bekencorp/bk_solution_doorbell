#include "bk_private/bk_init.h"
#include <os/os.h>
#include <string.h>

#include "doorbell_network_transfer.h"
#include "network_transfer.h"
#include "doorbell_cmd.h"
#include "doorbell_audio_device.h"
#include "doorbell_comm.h"

#define TAG "db-ntwk"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)


bk_err_t doorbell_bk_net_cntrl_recv(uint8_t *data, uint32_t length)
{
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

void doorbell_bk_net_msg_evt_handle(ntwk_trans_event_t *event)
{
    ntwk_trans_ctxt_t *ctxt = ntwk_trans_get_ctxt();
    doorbell_msg_t msg = {0};

    switch (event->code)
    {
        case NTWK_TRANS_EVT_START:
        {
            if (strcmp(ctxt->service_name, "tcp_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_VIDEO)
                {
                    msg.event = DBEVT_LAN_TCP_SERVICE_START_RESPONSE;
                }
            }
            else if (strcmp(ctxt->service_name, "udp_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_VIDEO)
                {
                    msg.event = DBEVT_LAN_UDP_SERVICE_START_RESPONSE;
                }
            }
            else if (strcmp(ctxt->service_name, "cs2_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    msg.event = DBEVT_P2P_CS2_SERVICE_START_RESPONSE;
                }
            }
        } break;
        case NTWK_TRANS_EVT_CONNECTED:
        {
            if(strcmp(ctxt->service_name, "tcp_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    msg.event = DBEVT_REMOTE_DEVICE_CONNECTED;
                }
            }
            else if(strcmp(ctxt->service_name, "udp_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    msg.event = DBEVT_REMOTE_DEVICE_CONNECTED;
                }
            }
        } break;
        case NTWK_TRANS_EVT_DISCONNECTED:
        {
            if(strcmp(ctxt->service_name, "tcp_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    msg.event = DBEVT_REMOTE_DEVICE_DISCONNECTED;
                }
                else if (event->chan_type == NTWK_TRANS_CHAN_VIDEO)
                {
                    if(msg.param == ENOTCONN)
                    {
                        msg.event = DBEVT_IMAGE_TCP_SERVICE_DISCONNECTED;
                    }
                    else
                    {
                        msg.event = DBEVT_REMOTE_DEVICE_DISCONNECTED;
                    }
                }
            }
        } break;
        case NTWK_TRANS_EVT_STOP:
        {
            if(strcmp(ctxt->service_name, "cs2_service") == 0)
            {
                if (event->chan_type == NTWK_TRANS_CHAN_CTRL)
                {
                    msg.event = DBEVT_REMOTE_DEVICE_DISCONNECTED;
                }
            }
        } break;
        default:
            break;
    }

    if (msg.event != 0)
    {
        msg.param = 0;
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