#ifndef __BK_KEY_INTERNAL_H__
#define __BK_KEY_INTERNAL_H__

#include "bk_key.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BK_KEY_TAG "bk_key"

/**
 * Enqueue a detected (key_id, gesture) into the engine dispatch queue.
 * Safe to call from any detector context (ADC timer, multi_button, SDK
 * key_thread). The engine applies the per-key gesture_mask filter, so the
 * driver may call this unconditionally.
 */
void bk_key_post_event(bk_key_id_t key_id, bk_key_gesture_t gesture);

/** Access the internal copy of the key table (valid after bk_key_init). */
const bk_key_cfg_t *bk_key_cfgs(uint8_t *num_out);

/* ---- driver layer (bk_key_driver.c), called by the engine ---- */

/** Bring up the SDK detector(s) for the given table. */
bk_err_t bk_key_driver_start(const bk_key_cfg_t *cfgs, uint8_t num);

/** Stop the SDK detector(s). */
void bk_key_driver_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK_KEY_INTERNAL_H__ */
