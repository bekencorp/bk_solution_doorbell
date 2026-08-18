#ifndef __DOORBELL_NETCFG_H__
#define __DOORBELL_NETCFG_H__

#include <common/bk_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file doorbell_netcfg.h
 *
 * WiFi provisioning + default auto-reconnect for the video_intercom project,
 * adapted from the AI solution's scheme (bk_smart_config over the SDK
 * bk_network_provisioning component).
 *
 * For video_intercom this REPLACES the legacy doorbell_boarding BLE transport
 * with the SDK's bk_ble_provisioning GATT service, while keeping all doorbell
 * business (CS2 P2P, server-net-info, LAN UDP/TCP) unchanged. The component
 * PROVIDES the doorbell_boarding.h symbols (doorbell_boarding_init,
 * doorbell_boarding_event_notify*, doorbell_boarding_operation_handle, ...)
 * backed by the SDK transport, so doorbell_core.c does not change and
 * doorbell_boarding.c is compiled out (see !CONFIG_DOORBELL_NETCFG).
 *
 * Boot behavior (default reconnect): doorbell_boarding_init() calls
 * bk_network_provisioning_init(BLE) which reconnects from NV "d_network_id" when
 * present (no advertising), otherwise starts BLE provisioning. The SDK persists
 * credentials on the first successful GOT_IP4, so later boots reconnect
 * automatically.
 *
 * Provisioning init is driven by doorbell_core_init() -> doorbell_boarding_init();
 * ap_main() does NOT need to call anything from this component directly. The
 * helpers below expose manual reconnect/erase/status and a "netcfg" CLI.
 */

/**
 * @brief UI lifecycle events forwarded to the application UI layer.
 *
 * doorbell_netcfg only reports provisioning transitions; it does not depend on
 * any UI implementation. The application (e.g. the video_intercom UI controller)
 * registers a callback via doorbell_netcfg_set_ui_event_cb() to react (show the
 * QR page, return to home on success, update status on failure).
 */
typedef enum {
    DBNP_UI_PROVISIONING, /* entered provisioning (RUNNING / RECONNECT_FAILED fallback) */
    DBNP_UI_SUCCESS,      /* provisioning / reconnect succeeded */
    DBNP_UI_FAILED,       /* provisioning failed */
} dbnp_ui_event_t;

/** UI event callback prototype (invoked on the SDK provisioning thread). */
typedef void (*dbnp_ui_event_cb_t)(dbnp_ui_event_t ev, void *user);

/**
 * @brief Register the UI event callback (NULL to unregister).
 *
 * The callback runs on the SDK provisioning status thread; keep it lightweight
 * and never block. Should be called before doorbell_core_init() so the very first
 * provisioning transition is not missed.
 */
void doorbell_netcfg_set_ui_event_cb(dbnp_ui_event_cb_t cb, void *user);

/**
 * @brief Manually reconnect to the saved network (from "d_network_id").
 * @return BK_OK if a reconnect was started, BK_FAIL if no valid credentials.
 */
bk_err_t doorbell_netcfg_reconnect(void);

/**
 * @brief Whether a valid STA network is currently persisted for reconnect.
 */
bool doorbell_netcfg_has_saved_network(void);

/**
 * @brief Erase the persisted reconnect info (forces re-provisioning).
 */
bk_err_t doorbell_netcfg_erase(void);

/**
 * @brief Start BLE provisioning on demand (e.g. from a button handler).
 * @return BK_OK on success.
 */
bk_err_t doorbell_netcfg_start_provisioning(void);

/**
 * @brief Provide the firmware version carried in the BLE provisioning
 *        advertisement (per the BLE provisioning adv spec).
 *
 * The application layer owns the version. doorbell_boarding_init() advertises the
 * spec core header {proto_ver, device_type=INTERCOM, fw_major, fw_minor,
 * fw_patch} inside the ADV Manufacturer Specific Data, and derives the Local Name
 * "BK_INTERCOM_<MAC3>". The device number is no longer part of the manufacturer
 * payload (it is conveyed by the MAC bytes in the Local Name).
 *
 * MUST be called before doorbell_core_init()/doorbell_boarding_init() so the very
 * first advertisement already carries the data.
 *
 * @param fw_major  Firmware major version.
 * @param fw_minor  Firmware minor version.
 * @param fw_patch  Firmware patch version.
 */
void doorbell_netcfg_set_adv_fw_version(uint8_t fw_major, uint8_t fw_minor, uint8_t fw_patch);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_NETCFG_H__ */
