#ifndef __KEY_MAP_H__
#define __KEY_MAP_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the physical keys (K3-K6) for the video_intercom board.
 *
 * Product-layer glue: describes the board's ADC key ladder and subscribes a
 * listener that maps device-agnostic (key_id, gesture) events onto doorbell
 * business (BLE provisioning, ring notify, factory erase). All business
 * coupling lives here, not in the reusable bk_key component.
 *
 * Call once after the core services are up (see ap_main.c). No-op unless
 * CONFIG_BK_KEY is enabled.
 */
void doorbell_key_start(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_MAP_H__ */
