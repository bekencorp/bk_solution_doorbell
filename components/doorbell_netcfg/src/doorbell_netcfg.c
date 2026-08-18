// Copyright 2020-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * doorbell_netcfg - WiFi provisioning + default auto-reconnect for video_intercom.
 *
 * This component adapts the AI solution's provisioning scheme (bk_smart_config
 * over the SDK bk_network_provisioning component) to the doorbell solution.
 * For the video_intercom project it REPLACES the legacy doorbell_boarding BLE
 * transport (bk_bluetooth/ble_boarding) with the SDK's bk_ble_provisioning GATT
 * service, while keeping all doorbell business logic (CS2 P2P, server-net-info,
 * LAN UDP/TCP services) unchanged by re-posting the exact same DBEVT_* messages
 * to the existing doorbell_core state machine.
 *
 * Because doorbell_core.c calls doorbell_boarding_event_notify() and
 * doorbell_boarding_init(), this component PROVIDES those symbols (declared in
 * doorbell_boarding.h) backed
 * by the SDK transport. doorbell_boarding.c is compiled out for this project via
 * !CONFIG_DOORBELL_NETCFG so the two never define the same symbol.
 *
 * Default reconnect: bk_network_provisioning_init(BLE) reconnects from NV key
 * "d_network_id" when present (no BLE advertising), otherwise starts BLE
 * provisioning. The SDK's own netif GOT_IP4 handler persists credentials into
 * "d_network_id" on the first successful provisioning connect, so subsequent
 * boots reconnect automatically.
 *
 * Only enable this (CONFIG_DOORBELL_NETCFG) in the video_intercom project; other
 * doorbell projects keep the legacy doorbell_boarding behavior.
 */

#include <common/bk_include.h>
#include <common/bk_err.h>
#include <stdio.h>
#include <string.h>
#include <os/str.h>
#include <os/mem.h>
#include <components/log.h>
#include <components/event.h>
#include <components/netif.h>

#include "bk_wifi.h"
#include "bk_network_provisioning.h"
#include "bk_ef.h"
#include "cli.h"
#include "components/bluetooth/bk_dm_bluetooth.h"

#include "doorbell_comm.h"
#include "doorbell_boarding.h"
#include "doorbell_network.h"
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
#include "network_transfer.h"
#endif

#include "doorbell_netcfg.h"

#define TAG "db-netcfg"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/* SDK NV key + "configured" flag bit, kept identical to bk_network_provisioning.c
 * so the credentials the SDK persists are picked up by
 * bk_network_auto_reconnect_init() on the next boot. */
#define DBNP_FCD_KEY        "d_network_id"
#define DBNP_FCD_FLAG_VALID 0x8000

/* WiFi channel hint received over BLE (BOARDING_OP_SET_WIFI_CHANNEL); currently
 * only consumed by the (disabled) SoftAP path, kept for protocol parity. */
static uint16_t s_wifi_channel;

/* Firmware version carried in the BLE advertisement core header (per the adv
 * spec). Supplied by the application via doorbell_netcfg_set_adv_fw_version()
 * before provisioning starts. Device type is fixed to INTERCOM for this project. */
static uint8_t s_adv_fw_major;
static uint8_t s_adv_fw_minor;
static uint8_t s_adv_fw_patch;

/* Application UI event callback (optional). Decouples this component from any UI
 * implementation: the video_intercom UI controller registers here to drive the
 * LVGL provisioning/home pages. */
static dbnp_ui_event_cb_t s_ui_cb;
static void              *s_ui_cb_user;

static inline void dbnp_ui_notify(dbnp_ui_event_t ev)
{
    if (s_ui_cb)
    {
        s_ui_cb(ev, s_ui_cb_user);
    }
}

void doorbell_netcfg_set_ui_event_cb(dbnp_ui_event_cb_t cb, void *user)
{
    s_ui_cb = cb;
    s_ui_cb_user = user;
}

void doorbell_netcfg_set_adv_fw_version(uint8_t fw_major, uint8_t fw_minor, uint8_t fw_patch)
{
	s_adv_fw_major = fw_major;
	s_adv_fw_minor = fw_minor;
	s_adv_fw_patch = fw_patch;
}

#ifdef CONFIG_CS2_P2P_SERVER
static p2p_cs2_key_t *s_p2p_cs2_key = NULL;
#endif

/* -------------------------------------------------------------------------- */
/* doorbell_boarding.h transport symbols, backed by the SDK BLE provisioning.   */
/* -------------------------------------------------------------------------- */

void doorbell_boarding_event_notify(uint16_t opcode, int status)
{
	bk_ble_provisioning_event_notify(opcode, status);
}

void doorbell_boarding_event_notify_with_data(uint16_t opcode, int status, char *payload, uint16_t length)
{
	bk_ble_provisioning_event_notify_with_data(opcode, status, payload, length);
}

int doorbell_boarding_notify(uint8_t *data, uint16_t length)
{
	return wifi_boarding_notify(data, length);
}

void doorbell_boarding_ble_disable(void)
{
	bk_ble_provisioning_deinit();
}

/* Mirror of doorbell_boarding.c's doorbell_boarding_event_message(): post the
 * ack to the doorbell_core thread so the reply is serialized with other work. */
static void dbnp_event_message(uint16_t opcode, int status)
{
	doorbell_msg_t msg;

	msg.event = DBEVT_START_BOARDING_EVENT;
	msg.param = ((uint32_t)status << 16) | opcode;
	doorbell_send_msg(&msg);
}

/*
 * Ported 1:1 from doorbell_boarding.c (SUPPORT_AP_PWD_ALL branch), with the only
 * change being where SSID/password come from: the SDK's provisioning info
 * (populated by the bk_ble_provisioning GATT characteristics) instead of the
 * legacy doorbell_boarding_info struct. Called both from the SDK BLE msg worker
 * (dbnp_ble_msg_handler) and from the CP CIFD path (smart_intercom
 * doorbell_config.c -> CIFD_EVENT_BLE_DATA_TO_USER).
 */
void doorbell_boarding_operation_handle(uint16_t opcode, uint16_t length, uint8_t *data)
{
	LOGD("%s, opcode: %04X, length: %u\n", __func__, opcode, length);

	switch (opcode)
	{
		case BOARDING_OP_STATION_START:
		{
			bk_ble_provisioning_info_t *bpi = bk_ble_provisioning_get_boarding_info();

			if (bpi == NULL || bpi->ble_prov_info.ssid_value == NULL)
			{
				LOGE("station start without ssid\n");
				break;
			}

			char *ssid = bpi->ble_prov_info.ssid_value;
			char *pwd = bpi->ble_prov_info.password_value ? bpi->ble_prov_info.password_value : "";

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
			doorbell_boarding_info_t bi;
			os_memset(&bi, 0, sizeof(bi));
			bi.boarding_info.ssid_value = ssid;
			bi.boarding_info.ssid_length = bpi->ble_prov_info.ssid_length;
			bi.boarding_info.password_value = pwd;
			bi.boarding_info.password_length = bpi->ble_prov_info.password_length;
			doorbell_save_wifi_info_to_flash(&bi);
#endif
			/* Connect on the SDK provisioning thread context; the SDK's
			 * netif/wifi event handlers drive RUNNING->SUCCEED and persist
			 * "d_network_id" on GOT_IP4, and doorbell_core notifies the phone
			 * with the acquired IP via DBEVT_WIFI_STATION_CONNECTED. */
			doorbell_wifi_sta_connect(ssid, pwd);
		}
		break;

		case BOARDING_OP_SOFT_AP_START:
		{
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
			bk_ble_provisioning_info_t *bpi = bk_ble_provisioning_get_boarding_info();

			if (bpi != NULL && bpi->ble_prov_info.ssid_value != NULL)
			{
				doorbell_boarding_info_t bi;
				os_memset(&bi, 0, sizeof(bi));
				bi.boarding_info.ssid_value = bpi->ble_prov_info.ssid_value;
				bi.boarding_info.ssid_length = bpi->ble_prov_info.ssid_length;
				bi.boarding_info.password_value = bpi->ble_prov_info.password_value;
				bi.boarding_info.password_length = bpi->ble_prov_info.password_length;
				doorbell_save_wifi_info_to_flash(&bi);
			}
#endif
			doorbell_msg_t msg;
			msg.event = DBEVT_WIFI_SOFT_AP_TURNING_ON;
			msg.param = 0;
			doorbell_send_msg(&msg);
		}
		break;

		case BOARDING_OP_SERVICE_UDP_START:
		{
			doorbell_msg_t msg;
			msg.event = DBEVT_LAN_UDP_SERVICE_START_REQUEST;
			msg.param = 0;
			doorbell_send_msg(&msg);
		}
		break;

		case BOARDING_OP_SERVICE_TCP_START:
		{
			doorbell_msg_t msg;
			msg.event = DBEVT_LAN_TCP_SERVICE_START_REQUEST;
			msg.param = 0;
			doorbell_send_msg(&msg);
		}
		break;

#ifdef CONFIG_CS2_P2P_SERVER
		case BOARDING_OP_SET_CS2_DID:
		{
			if (s_p2p_cs2_key == NULL)
			{
				s_p2p_cs2_key = os_malloc(sizeof(p2p_cs2_key_t));
				if (s_p2p_cs2_key == NULL)
				{
					LOGE("malloc p2p_cs2_key\n");
					break;
				}
				os_memset(s_p2p_cs2_key, 0, sizeof(p2p_cs2_key_t));
			}

			if (strlen(s_p2p_cs2_key->did))
			{
				LOGE("Already has did %s\n", s_p2p_cs2_key->did);
				break;
			}
			if (length > sizeof(s_p2p_cs2_key->did))
			{
				LOGE("payload[%d] > did size[%d]\n", length, sizeof(s_p2p_cs2_key->did));
				break;
			}
			os_memcpy(s_p2p_cs2_key->did, data, length);
			LOGD("did: %s\n", s_p2p_cs2_key->did);
			dbnp_event_message(opcode, BK_OK);
		}
		break;

		case BOARDING_OP_SET_CS2_APILICENSE:
		{
			if (s_p2p_cs2_key == NULL)
			{
				s_p2p_cs2_key = os_malloc(sizeof(p2p_cs2_key_t));
				if (s_p2p_cs2_key == NULL)
				{
					LOGE("malloc p2p_cs2_key\n");
					break;
				}
				os_memset(s_p2p_cs2_key, 0, sizeof(p2p_cs2_key_t));
			}

			if (strlen(s_p2p_cs2_key->apilicense))
			{
				LOGE("Already has apilicense %s\n", s_p2p_cs2_key->apilicense);
				break;
			}
			if (length > sizeof(s_p2p_cs2_key->apilicense))
			{
				LOGE("payload[%d] > apilicense size[%d]\n", length, sizeof(s_p2p_cs2_key->apilicense));
				break;
			}
			os_memcpy(s_p2p_cs2_key->apilicense, data, length);
			LOGD("apilicense: %s\n", s_p2p_cs2_key->apilicense);
			dbnp_event_message(opcode, BK_OK);
		}
		break;

		case BOARDING_OP_SET_CS2_KEY:
		{
			if (s_p2p_cs2_key == NULL)
			{
				s_p2p_cs2_key = os_malloc(sizeof(p2p_cs2_key_t));
				if (s_p2p_cs2_key == NULL)
				{
					LOGE("malloc p2p_cs2_key\n");
					break;
				}
				os_memset(s_p2p_cs2_key, 0, sizeof(p2p_cs2_key_t));
			}

			if (strlen(s_p2p_cs2_key->key))
			{
				LOGE("Already key %s\n", s_p2p_cs2_key->key);
				break;
			}
			if (length > sizeof(s_p2p_cs2_key->key))
			{
				LOGE("payload[%d] > key size[%d]\n", length, sizeof(s_p2p_cs2_key->key));
				break;
			}
			os_memcpy(s_p2p_cs2_key->key, data, length);
			LOGD("key: %s\n", s_p2p_cs2_key->key);
			dbnp_event_message(opcode, BK_OK);
		}
		break;

		case BOARDING_OP_SET_CS2_INIT_STRING:
		{
			if (s_p2p_cs2_key == NULL)
			{
				s_p2p_cs2_key = os_malloc(sizeof(p2p_cs2_key_t));
				if (s_p2p_cs2_key == NULL)
				{
					LOGE("malloc p2p_cs2_key\n");
					break;
				}
				os_memset(s_p2p_cs2_key, 0, sizeof(p2p_cs2_key_t));
			}

			if (strlen(s_p2p_cs2_key->initstring))
			{
				LOGE("Already has initstring %s\n", s_p2p_cs2_key->initstring);
				break;
			}
			if (length > sizeof(s_p2p_cs2_key->initstring))
			{
				LOGE("payload[%d] > initstring size[%d]\n", length, sizeof(s_p2p_cs2_key->initstring));
				break;
			}
			os_memcpy(s_p2p_cs2_key->initstring, data, length);
			LOGD("initstring: %s\n", s_p2p_cs2_key->initstring);
			dbnp_event_message(opcode, BK_OK);
		}
		break;

		case BOARDING_OP_SRRVICE_CS2_START:
		{
			if (s_p2p_cs2_key == NULL)
			{
				LOGE("cs2 key not set\n");
				break;
			}
			if (s_p2p_cs2_key->cs2_started)
			{
				LOGE("CS2 already started %x\n", s_p2p_cs2_key->cs2_started);
				break;
			}

			strcat(s_p2p_cs2_key->apilicense, ":");
			strcat(s_p2p_cs2_key->apilicense, s_p2p_cs2_key->key);
			strcat(s_p2p_cs2_key->initstring, ":");
			strcat(s_p2p_cs2_key->initstring, s_p2p_cs2_key->key);
			s_p2p_cs2_key->cs2_started = true;

			doorbell_msg_t msg;
			msg.event = DBEVT_P2P_CS2_SERVICE_START_REQUEST;
			msg.param = (uint32_t)s_p2p_cs2_key;
			doorbell_send_msg(&msg);
		}
		break;
#endif /* CONFIG_CS2_P2P_SERVER */

		case BOARDING_OP_BLE_DISABLE:
		{
			doorbell_msg_t msg;
			msg.event = DBEVT_BLE_DISABLE;
			msg.param = 0;
			doorbell_send_msg(&msg);
		}
		break;

		case BOARDING_OP_SET_WIFI_CHANNEL:
		{
			if (length >= 2 && data != NULL)
			{
				s_wifi_channel = data[0] | (data[1] << 8);
				LOGD("%s, BOARDING_OP_SET_WIFI_CHANNEL: %u\n", __func__, s_wifi_channel);
			}
		}
		break;

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
		case BOARDING_OP_SET_SERVER_NET_INFO:
		{
			LOGD("BOARDING_OP_SET_SERVER_NET_INFO\n");
			doorbell_save_server_net_info_to_flash(data);

			doorbell_msg_t msg;
			msg.event = DBEVT_SET_SERVER_NET_INFO;
			msg.param = 0;
			doorbell_send_msg(&msg);
		}
		break;

		case BOARDING_OP_CONNECTION_SERVER_FAILED:
		{
			LOGD("%s, BOARDING_OP_CONNECTION_SERVER_FAILED\n", __func__);
		}
		break;
#endif

		default:
			LOGW("unhandled opcode %04X\n", opcode);
			break;
	}
}

/* SDK BLE msg-handler: runs on bk_ble_provisioning's worker thread and forwards
 * the decoded opcode/payload to the shared doorbell dispatcher above. */
static void dbnp_ble_msg_handler(ble_prov_msg_t *msg)
{
	if (msg == NULL)
	{
		return;
	}
	doorbell_boarding_operation_handle((uint16_t)msg->event, msg->length, (uint8_t *)msg->param);
}

static void dbnp_status_cb(bk_network_provisioning_status_t status, void *user_data)
{
	(void)user_data;

	switch (status)
	{
		case BK_NETWORK_PROVISIONING_STATUS_RUNNING:
			LOGI("network provisioning running\r\n");
			/* No saved creds -> the SDK entered provisioning mode. Ask the UI
			 * layer to show the provisioning page (after boot media). */
			dbnp_ui_notify(DBNP_UI_PROVISIONING);
			break;
		case BK_NETWORK_PROVISIONING_STATUS_SUCCEED:
			LOGI("network provisioning succeed\r\n");
			/* Send the opcode-1 (BOARDING_OP_STATION_START) completion reply to
			 * the phone. The STATION_START handler relied on the legacy
			 * bk_wlan_status cb (doorbell_wifi_event_cb) posting
			 * DBEVT_WIFI_STATION_CONNECTED on GOT_IP, but in
			 * CONFIG_WIFI_VNET_CONTROLLER builds the netif static status cb
			 * early-returns once the CP already notified GOT_IP4, so that legacy
			 * cb never fires and the phone times out waiting for the reply.
			 * Post the same event here from the SDK provisioning status cb (which
			 * does fire) so doorbell_core replies with the acquired STA IP. */
			{
				doorbell_msg_t reply_msg;
				reply_msg.event = DBEVT_WIFI_STATION_CONNECTED;
				reply_msg.param = 0;
				doorbell_send_msg(&reply_msg);
			}
			dbnp_ui_notify(DBNP_UI_SUCCESS);
			break;
		case BK_NETWORK_PROVISIONING_STATUS_FAILED:
			LOGW("network provisioning failed\r\n");
			dbnp_ui_notify(DBNP_UI_FAILED);
			break;
		case BK_NETWORK_PROVISIONING_STATUS_RECONNECTING:
			LOGI("reconnecting...\r\n");
			break;
		case BK_NETWORK_PROVISIONING_STATUS_RECONNECT_SUCCEED:
			LOGI("reconnect succeed\r\n");
			dbnp_ui_notify(DBNP_UI_SUCCESS);
			break;
		case BK_NETWORK_PROVISIONING_STATUS_RECONNECT_FAILED:
			LOGW("reconnect failed\r\n");
			/* Cannot auto-connect to the saved network -> fall back to BLE
			 * provisioning and ask the UI to show the QR page so the user can
			 * re-provision. Only takes visible effect when a UI callback is
			 * registered. */
			bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
			dbnp_ui_notify(DBNP_UI_PROVISIONING);
			break;
		default:
			LOGD("status:%d\r\n", status);
			break;
	}
}

/* -------------------------------------------------------------------------- */
/* Public helpers + CLI                                                        */
/* -------------------------------------------------------------------------- */

static bk_err_t dbnp_load(BK_FAST_CONNECT_D *out)
{
	if (out == NULL)
	{
		return BK_FAIL;
	}

	os_memset(out, 0, sizeof(*out));

	if (bk_get_env_enhance(DBNP_FCD_KEY, (void *)out, sizeof(*out)) <= 0)
	{
		return BK_FAIL;
	}

	if ((out->flag & DBNP_FCD_FLAG_VALID) != DBNP_FCD_FLAG_VALID)
	{
		return BK_FAIL;
	}

	if (!(out->flag & BIT(NETIF_IF_STA)) || out->sta_ssid[0] == '\0')
	{
		return BK_FAIL;
	}

	return BK_OK;
}

bool doorbell_netcfg_has_saved_network(void)
{
	BK_FAST_CONNECT_D info;

	return (dbnp_load(&info) == BK_OK);
}

bk_err_t doorbell_netcfg_reconnect(void)
{
	BK_FAST_CONNECT_D info;
	wifi_sta_config_t sta_config = {0};

	if (dbnp_load(&info) != BK_OK)
	{
		LOGI("no saved network, skip reconnect\r\n");
		return BK_FAIL;
	}

	os_strcpy(sta_config.ssid, (char *)info.sta_ssid);
	os_strcpy(sta_config.password, (char *)info.sta_pwd);

	LOGI("reconnect to saved AP, ssid:%s\r\n", sta_config.ssid);

	BK_LOG_ON_ERR(bk_wifi_sta_set_config(&sta_config));
	BK_LOG_ON_ERR(bk_wifi_sta_start());

	return BK_OK;
}

bk_err_t doorbell_netcfg_erase(void)
{
	/* Ported from the AI solution's bk_sconf_erase_smart_config(): clear the
	 * persisted reconnect info AND drop the current STA link so the device is
	 * back to an unprovisioned state without a reboot. (The doorbell has no
	 * ai-agent channel-name/token to clear, so those AI-specific steps are
	 * omitted.) */
	erase_network_auto_reconnect_info();

	bk_err_t ret = bk_wifi_sta_stop();
	if (ret != BK_OK)
	{
		LOGW("stop Wi-Fi STA after erase failed, ret=%d\r\n", ret);
	}

	LOGI("erase saved network\r\n");

	return BK_OK;
}

bk_err_t doorbell_netcfg_start_provisioning(void)
{
	LOGI("start BLE provisioning on demand\r\n");
	return bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
}

static void cli_netcfg(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
	if (argc >= 2 && os_strcmp(argv[1], "reconnect") == 0)
	{
		doorbell_netcfg_reconnect();
	}
	else if (argc >= 2 && os_strcmp(argv[1], "erase") == 0)
	{
		doorbell_netcfg_erase();
	}
	else if (argc >= 2 && os_strcmp(argv[1], "prov") == 0)
	{
		doorbell_netcfg_start_provisioning();
	}
	else /* status / default */
	{
		LOGI("provisioned: %s\r\n", doorbell_netcfg_has_saved_network() ? "yes" : "no");
	}
}

#define DBNP_CMD_CNT (sizeof(s_netcfg_commands) / sizeof(struct cli_command))
static const struct cli_command s_netcfg_commands[] = {
	{"netcfg", "netcfg [reconnect|erase|prov|status]", cli_netcfg},
};

/* -------------------------------------------------------------------------- */
/* Boot init - provides doorbell_boarding_init() (called by doorbell_core_init) */
/* -------------------------------------------------------------------------- */

int doorbell_boarding_init(void)
{
	LOGI("%s (SDK bk_network_provisioning transport)\r\n", __func__);

	cli_register_commands(s_netcfg_commands, DBNP_CMD_CNT);

	/* Receive provisioning status transitions (RUNNING/SUCCEED/RECONNECT_*). */
	bk_register_network_provisioning_status_cb(dbnp_status_cb);

#if CONFIG_BK_BLE_PROVISIONING
	/* Advertise per the BLE provisioning adv spec: Local Name "BK_INTERCOM_<MAC3>"
	 * and the 5-byte core header {proto_ver, device_type=INTERCOM, fw x3} inside
	 * the ADV Manufacturer Specific Data. bk_bluetooth_get_address() is readable
	 * before the BT stack is brought up (the legacy doorbell_boarding used it the
	 * same way); the SDK consumes both later in wifi_boarding_adv_start(). */
	{
		uint8_t mac[6] = {0};
		char adv_name[32] = {0};

		bk_bluetooth_get_address(mac);
		snprintf(adv_name, sizeof(adv_name), "BK_%s_%02X%02X%02X",
		         bk_ble_provisioning_dev_type_tag(BK_BLE_PROV_DEV_TYPE_INTERCOM),
		         mac[0], mac[1], mac[2]);
		bk_ble_provisioning_set_adv_name(adv_name);

		bk_ble_provisioning_set_dev_info(BK_BLE_PROV_DEV_TYPE_INTERCOM,
		                                 s_adv_fw_major, s_adv_fw_minor, s_adv_fw_patch);
	}

	/* Dispatch decoded BLE opcodes into the doorbell business handler. */
	bk_ble_provisioning_set_msg_handle_cb(dbnp_ble_msg_handler);
#endif

	/* Default reconnect: if "d_network_id" holds valid STA credentials the SDK
	 * reconnects silently (no BLE advertising); otherwise it starts BLE
	 * provisioning so the phone can configure WiFi. */
	bk_network_provisioning_init(BK_NETWORK_PROVISIONING_TYPE_BLE);

	return BK_OK;
}
