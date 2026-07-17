#ifndef _DOORBELL_SELFTEST_H_
#define _DOORBELL_SELFTEST_H_

#include <common/bk_include.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The self-loopback test harness (auto_test/doorbell_selftest.c) is gated by
 * CONFIG_SMART_INTERCOM_AUTO_TEST. When it is disabled the .c is NOT compiled,
 * so this header supplies inline no-op stubs: production call sites
 * (ap_main.c -> doorbell_selftest_cli_init, doorbell_devices.c uplink task ->
 * doorbell_selftest_downlink_tee_feed) keep compiling unchanged and simply do
 * nothing, leaving zero test code in the shipped image.
 */
#if CONFIG_SMART_INTERCOM_AUTO_TEST

/**
 * @brief Register the "db_selftest" CLI used for on-device (no-APK) loopback
 *        self-test of the bidirectional video-intercom features.
 *
 * Call once from main() after the doorbell core is initialized.
 *
 * @return BK_OK on success.
 */
int doorbell_selftest_cli_init(void);

/**
 * @brief Uplink-to-downlink loopback tee.
 *
 * The uplink transfer task calls this once per encoded access unit. When the
 * downlink loopback is armed (via "db_selftest downlink on"), a copy of the AU
 * is fed to the downlink H.264 decode path. It is a cheap no-op otherwise.
 *
 * @param data Pointer to the encoded H.264 access unit bytes.
 * @param len  Length of the access unit in bytes.
 */
void doorbell_selftest_downlink_tee_feed(uint8_t *data, uint32_t len);

#else /* !CONFIG_SMART_INTERCOM_AUTO_TEST */

static inline int doorbell_selftest_cli_init(void)
{
    return 0;
}

static inline void doorbell_selftest_downlink_tee_feed(uint8_t *data, uint32_t len)
{
    (void)data;
    (void)len;
}

#endif /* CONFIG_SMART_INTERCOM_AUTO_TEST */

#ifdef __cplusplus
}
#endif

#endif /* _DOORBELL_SELFTEST_H_ */
