#include "bk_private/bk_init.h"
#include <os/os.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "network_transfer.h"
#include "doorbell_cmd.h"
#include "doorbell_audio_device.h"
#include "doorbell_comm.h"
#include "doorbell_network_transfer.h"
#include "common/network_transfer_common.h"


#define TAG "db-kvs-ntwk"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)


void doorbell_kvs_get_format_utc_ts(char *buf, size_t len)
{
    struct timeval tv;
    struct tm tm_utc;
    time_t sec;

    if (buf == NULL || len == 0) {
        return;
    }
    if (gettimeofday(&tv, NULL) != 0) {
        snprintf(buf, len, "utc(n/a)");
        return;
    }
    sec = (time_t)tv.tv_sec;
    if (gmtime_r(&sec, &tm_utc) == NULL) {
        snprintf(buf, len, "utc(n/a)");
        return;
    }
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1,
             tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, (int)(tv.tv_usec / 1000));
}

static bk_err_t doorbell_kvs_net_cntrl_recv(uint8_t *data, uint32_t length)
{
	doorbell_transmission_cmd_recive_callback(data, length);
	return BK_OK;
}

static bk_err_t doorbell_kvs_net_video_recv(uint8_t *data, uint32_t length)
{
	(void)data;
	(void)length;
	return BK_OK;
}

static bk_err_t doorbell_kvs_net_audio_recv(uint8_t *data, uint32_t length)
{
#ifdef CONFIG_VOICE_SERVICE
	doorbell_audio_data_callback(data, length);
#endif
	(void)data;
	(void)length;
	return BK_OK;
}

static void doorbell_kvs_net_msg_evt_handle(ntwk_trans_event_t *event)
{
	(void)event;
}

static bk_err_t doorbell_kvs_network_transfer_start(char *service_name, void *param)
{
	(void)param;
	LOGI("%s\n", __func__);

	ntwk_trans_register_msg_event_cb(doorbell_kvs_net_msg_evt_handle);
	ntwk_trans_register_ctrl_recv_cb(doorbell_kvs_net_cntrl_recv);
	ntwk_trans_chan_start(NTWK_TRANS_CHAN_CTRL, NULL);

	ntwk_trans_register_video_recv_cb(doorbell_kvs_net_video_recv);
	ntwk_trans_chan_start(NTWK_TRANS_CHAN_VIDEO, NULL);

	ntwk_trans_register_audio_recv_cb(doorbell_kvs_net_audio_recv);
	ntwk_trans_chan_start(NTWK_TRANS_CHAN_AUDIO, NULL);

	(void)service_name;
	return BK_OK;
}

static bk_err_t doorbell_kvs_network_transfer_stop(void)
{
	ntwk_trans_chan_stop(NTWK_TRANS_CHAN_CTRL);
	ntwk_trans_chan_stop(NTWK_TRANS_CHAN_VIDEO);
	ntwk_trans_chan_stop(NTWK_TRANS_CHAN_AUDIO);
	return BK_OK;
}

bk_err_t doorbell_kvs_ntwk_init(char *service_name, void *param)
{
	if (strcmp(service_name, "kvs_service") != 0) {
		LOGE("unsupported service %s\n", service_name);
		return BK_FAIL;
	}
#if CONFIG_KVS_NTWK_BRIDGE
	bk_kvs_trans_service_init(service_name);
	doorbell_kvs_network_transfer_start(service_name, param);
#else
	(void)param;
	LOGE("CONFIG_KVS_NTWK_BRIDGE is off\n");
	return BK_FAIL;
#endif
	return BK_OK;
}

bk_err_t doorbell_kvs_ntwk_deinit(char *service_name)
{
	(void)service_name;
	doorbell_kvs_network_transfer_stop();
#if CONFIG_KVS_NTWK_BRIDGE
	bk_kvs_trans_service_deinit();
#endif
	return BK_OK;
}

/* doorbell_core.c expects this symbol (LAN UDP/TCP/CS2 path); KVS product uses kvs_service from ap_main. */
bk_err_t doorbell_bk_network_transfer_init(char *service_name, void *param)
{
	if (service_name != NULL && strcmp(service_name, "kvs_service") == 0) {
		return doorbell_kvs_ntwk_init(service_name, param);
	}
	LOGW("%s: ignore legacy transfer service '%s' (KVS build)\n", __func__, service_name ? service_name : "(null)");
	(void)param;
	return BK_OK;
}

bk_err_t doorbell_bk_network_transfer_deinit(char *service_name)
{
	if (service_name != NULL && strcmp(service_name, "kvs_service") == 0) {
		return doorbell_kvs_ntwk_deinit(service_name);
	}
	return BK_OK;
}
