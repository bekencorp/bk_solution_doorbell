#ifndef __DOORBELL_BLE_ADV_H__
#define __DOORBELL_BLE_ADV_H__

#include <stdint.h>
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * doorbell_ble_adv - project-local, spec-compliant BLE provisioning advertiser
 * for the video_intercom project ONLY.
 *
 * Why this exists (see "BK7259 BLE 配网广播与 Scan Response 数据规范"):
 * video_intercom builds with CONFIG_BT undefined, so the SDK's
 * bk_network_provisioning compiles the legacy "//ble" (ble_boarding) branch of
 * wifi_boarding_adv_start(). That branch emits only the 2-byte Beken company ID
 * in the Manufacturer AD and never appends the 5-byte core header
 * {proto_ver, device_type, fw_major, fw_minor, fw_patch} that
 * bk_ble_provisioning_set_dev_info() stored. As a result the provisioning
 * advertisement carries no version / private-protocol information.
 *
 * Instead of patching the shared SDK (which would affect every project), this
 * module re-drives the SAME single legacy advertising instance (actv 0, the one
 * ble_boarding created) with a compliant packet built exactly like the dashboard
 * solution does: ADV = Flags + Manufacturer(company + 5B core header) + Service
 * Data(0xFE01), and the Local Name dynamically placed in ADV (<=17 chars) or the
 * Scan Response. It reuses actv 0 (CONFIG_BLE_ADV_NUM == 1, only one instance) so
 * it never conflicts with the SDK's GATT service.
 *
 * This whole component is compiled only when CONFIG_DOORBELL_NETCFG=y, which is
 * set exclusively in the video_intercom defconfig, so the other doorbell
 * projects (doorbell, doorbell_lp, doorbell_lp_QN128B832) are unaffected.
 */

/**
 * @brief Schedule a deferred take-over of the provisioning advertisement.
 *
 * Must be called when BLE provisioning has entered the RUNNING state. The SDK
 * starts its (incomplete) advertising slightly after the RUNNING status
 * callback, so this spawns a short one-shot worker that waits for the SDK adv to
 * come up and then reconfigures actv 0 with the spec-compliant packet. Safe to
 * call repeatedly (e.g. on every RUNNING transition); overlapping schedules are
 * ignored while a worker is still pending.
 *
 * @param device_type On-wire device type byte (BK_BLE_PROV_DEV_TYPE_INTERCOM).
 * @param fw_major    Firmware major version.
 * @param fw_minor    Firmware minor version.
 * @param fw_patch    Firmware patch version.
 * @param name        Complete Local Name to advertise (e.g. "BK_INTERCOM_ABCDEF").
 * @return BK_OK on schedule success.
 */
bk_err_t doorbell_ble_adv_schedule(uint8_t device_type,
                                   uint8_t fw_major,
                                   uint8_t fw_minor,
                                   uint8_t fw_patch,
                                   const char *name);

/** @brief Stop the pending worker (best effort). Called on provisioning teardown. */
void doorbell_ble_adv_cancel(void);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_BLE_ADV_H__ */
