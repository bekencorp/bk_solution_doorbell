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
 * See doorbell_ble_adv.h for the full rationale. In short: re-drive the single
 * legacy advertising instance the SDK's ble_boarding created (actv 0) with a
 * BLE-provisioning-spec-compliant ADV + Scan Response so the video_intercom
 * broadcast carries the version / private-protocol core header.
 *
 * This file is compiled only for the video_intercom project (the whole
 * doorbell_netcfg component is gated by CONFIG_DOORBELL_NETCFG).
 */

#include <string.h>
#include <stdbool.h>

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include <common/bk_err.h>

#include "ble_api_5_x.h"

#include "doorbell_ble_adv.h"

#define TAG "db-bleadv"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Per the adv spec / bk_network_provisioning constants. */
#define DB_ADV_COMPANY_ID     0x05F0
#define DB_ADV_PROTO_VERSION  0x01
#define DB_ADV_BOARDING_UUID  0xFE01

/* AD types (Bluetooth CSS). */
#define DB_AD_TYPE_FLAGS      0x01
#define DB_AD_TYPE_NAME_CMPL  0x09
#define DB_AD_TYPE_SERVICE_DATA 0x16
#define DB_AD_TYPE_MANU       0xFF

/* ble_boarding created exactly one legacy adv instance (CONFIG_BLE_ADV_NUM==1). */
#define DB_ADV_ACTV_IDX       0

#define DB_ADV_CMD_TIMEOUT_MS 4000

/* Give the SDK's bk_ble_np_init() time to bring up ble_boarding's advertising
 * before we reconfigure it (the RUNNING status callback fires just BEFORE the
 * SDK starts advertising). */
#define DB_ADV_TAKEOVER_DELAY_MS 800
#define DB_ADV_TAKEOVER_RETRIES  4
#define DB_ADV_TAKEOVER_GAP_MS   500

#define DB_ADV_TASK_STACK        2048
#define DB_ADV_NAME_MAX          32

static beken_semaphore_t s_cmd_sema;
static volatile ble_err_t s_cmd_status;

static beken_thread_t s_worker;
static volatile bool s_worker_run;

static uint8_t s_device_type;
static uint8_t s_fw_major;
static uint8_t s_fw_minor;
static uint8_t s_fw_patch;
static char    s_name[DB_ADV_NAME_MAX];

static void db_adv_cmd_cb(ble_cmd_t cmd, ble_cmd_param_t *param)
{
	s_cmd_status = param->status;

	switch (cmd) {
	case BLE_STOP_ADV:
	case BLE_SET_ADV_DATA:
	case BLE_SET_RSP_DATA:
	case BLE_START_ADV:
		if (s_cmd_sema != NULL) {
			rtos_set_semaphore(&s_cmd_sema);
		}
		break;
	default:
		break;
	}
}

/* Issue one legacy BLE command and wait for its completion. Returns BK_OK only
 * when both the synchronous submit and the async controller result succeed. */
static bk_err_t db_adv_do(ble_err_t submit_ret, const char *step)
{
	if (submit_ret != BK_ERR_BLE_SUCCESS) {
		LOGW("%s submit failed %d\r\n", step, submit_ret);
		return BK_FAIL;
	}

	if (rtos_get_semaphore(&s_cmd_sema, DB_ADV_CMD_TIMEOUT_MS) != BK_OK) {
		LOGW("%s wait timeout\r\n", step);
		return BK_FAIL;
	}

	if (s_cmd_status != BK_ERR_BLE_SUCCESS) {
		LOGW("%s controller err %d\r\n", step, s_cmd_status);
		return BK_FAIL;
	}

	return BK_OK;
}

/* Build the compliant ADV + Scan Response and re-drive actv 0. */
static bk_err_t db_adv_apply(void)
{
	uint8_t adv[31];
	uint8_t rsp[31];
	uint8_t adv_len = 0;
	uint8_t rsp_len = 0;
	size_t name_len = os_strlen(s_name);
	bool name_in_adv = false;

	/* Flags: LE General Discoverable + BR/EDR Not Supported. */
	adv[adv_len++] = 0x02;
	adv[adv_len++] = DB_AD_TYPE_FLAGS;
	adv[adv_len++] = 0x06;

	/* Manufacturer Specific Data: company ID + 5-byte core header. */
	adv[adv_len++] = 0x08;
	adv[adv_len++] = DB_AD_TYPE_MANU;
	adv[adv_len++] = DB_ADV_COMPANY_ID & 0xFF;
	adv[adv_len++] = (DB_ADV_COMPANY_ID >> 8) & 0xFF;
	adv[adv_len++] = DB_ADV_PROTO_VERSION;
	adv[adv_len++] = s_device_type;
	adv[adv_len++] = s_fw_major;
	adv[adv_len++] = s_fw_minor;
	adv[adv_len++] = s_fw_patch;

	/* 16-bit Service Data (kept for legacy scan filters keyed on 0xFE01). */
	adv[adv_len++] = 0x03;
	adv[adv_len++] = DB_AD_TYPE_SERVICE_DATA;
	adv[adv_len++] = DB_ADV_BOARDING_UUID & 0xFF;
	adv[adv_len++] = (DB_ADV_BOARDING_UUID >> 8) & 0xFF;

	/* Local Name: keep in ADV when it still fits (AD header = 2 bytes),
	 * otherwise defer the whole name to the Scan Response. */
	if (name_len > 0 && (adv_len + 2 + name_len) <= sizeof(adv)) {
		adv[adv_len++] = (uint8_t)(name_len + 1);
		adv[adv_len++] = DB_AD_TYPE_NAME_CMPL;
		os_memcpy(&adv[adv_len], s_name, name_len);
		adv_len += (uint8_t)name_len;
		name_in_adv = true;
	}

	if (!name_in_adv && name_len > 0) {
		if (name_len > (size_t)(sizeof(rsp) - 2)) {
			name_len = sizeof(rsp) - 2; /* truncate to fit */
		}
		rsp[rsp_len++] = (uint8_t)(name_len + 1);
		rsp[rsp_len++] = DB_AD_TYPE_NAME_CMPL;
		os_memcpy(&rsp[rsp_len], s_name, name_len);
		rsp_len += (uint8_t)name_len;
	}

	/* Stop the SDK's advertising on actv 0 (best effort: if it is not up yet
	 * the submit fails and we simply skip the wait and retry the whole apply). */
	if (bk_ble_stop_advertising(DB_ADV_ACTV_IDX, db_adv_cmd_cb) == BK_ERR_BLE_SUCCESS) {
		(void)rtos_get_semaphore(&s_cmd_sema, DB_ADV_CMD_TIMEOUT_MS);
	}

	if (db_adv_do(bk_ble_set_adv_data(DB_ADV_ACTV_IDX, adv, adv_len, db_adv_cmd_cb),
	              "set_adv_data") != BK_OK) {
		return BK_FAIL;
	}

	if (rsp_len > 0) {
		if (db_adv_do(bk_ble_set_scan_rsp_data(DB_ADV_ACTV_IDX, rsp, rsp_len, db_adv_cmd_cb),
		              "set_scan_rsp") != BK_OK) {
			return BK_FAIL;
		}
	}

	if (db_adv_do(bk_ble_start_advertising(DB_ADV_ACTV_IDX, 0, db_adv_cmd_cb),
	              "start_adv") != BK_OK) {
		return BK_FAIL;
	}

	LOGI("compliant adv applied: dev_type=0x%02X fw=%u.%u.%u name=%s adv_len=%u rsp_len=%u\r\n",
	     s_device_type, s_fw_major, s_fw_minor, s_fw_patch, s_name, adv_len, rsp_len);
	return BK_OK;
}

static void db_adv_worker(beken_thread_arg_t arg)
{
	(void)arg;

	rtos_delay_milliseconds(DB_ADV_TAKEOVER_DELAY_MS);

	for (int i = 0; i < DB_ADV_TAKEOVER_RETRIES && s_worker_run; i++) {
		if (db_adv_apply() == BK_OK) {
			break;
		}
		rtos_delay_milliseconds(DB_ADV_TAKEOVER_GAP_MS);
	}

	s_worker = NULL;
	rtos_delete_thread(NULL);
}

bk_err_t doorbell_ble_adv_schedule(uint8_t device_type,
                                   uint8_t fw_major,
                                   uint8_t fw_minor,
                                   uint8_t fw_patch,
                                   const char *name)
{
	bk_err_t ret;

	s_device_type = device_type;
	s_fw_major = fw_major;
	s_fw_minor = fw_minor;
	s_fw_patch = fw_patch;
	os_memset(s_name, 0, sizeof(s_name));
	if (name != NULL) {
		os_strncpy(s_name, name, sizeof(s_name) - 1);
	}

	if (s_worker != NULL) {
		/* A take-over is already pending; it will pick up the latest info. */
		return BK_OK;
	}

	if (s_cmd_sema == NULL) {
		ret = rtos_init_semaphore(&s_cmd_sema, 1);
		if (ret != BK_OK) {
			LOGE("cmd sema init failed %d\r\n", ret);
			return ret;
		}
	}

	s_worker_run = true;
	ret = rtos_create_thread(&s_worker, BEKEN_DEFAULT_WORKER_PRIORITY,
	                         "db_adv",
	                         (beken_thread_function_t)db_adv_worker,
	                         DB_ADV_TASK_STACK, NULL);
	if (ret != BK_OK) {
		LOGE("worker create failed %d\r\n", ret);
		s_worker_run = false;
		return ret;
	}

	return BK_OK;
}

void doorbell_ble_adv_cancel(void)
{
	s_worker_run = false;
}
