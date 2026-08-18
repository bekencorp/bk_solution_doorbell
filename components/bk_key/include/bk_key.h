#ifndef __BK_KEY_H__
#define __BK_KEY_H__

#include <common/bk_err.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file bk_key.h
 *
 * Generic, business-agnostic button engine.
 *
 * The component only produces device-agnostic events of the form
 * (key_id, gesture). It does NOT know what a key means (volume, ring,
 * provisioning, ...). Products describe their hardware with a runtime
 * bk_key_cfg_t[] table and react by subscribing a listener. This keeps the
 * component free of any business dependency and trivially portable across
 * solutions (doorbell / dashboard / ai) and boards.
 *
 * Detection is delegated to the SDK drivers:
 *   - ADC  keys  -> ap/components/adc_key (resistor-ladder, one voltage
 *                   window per key on a shared ADC channel).
 *   - GPIO keys  -> ap/components/key     (multi_button polling state machine).
 *
 * Regardless of the source, listener callbacks always run in the component's
 * own dispatch thread (unified context).
 */

/** Device-agnostic gesture. */
typedef enum {
	BK_KEY_GESTURE_SHORT = 0,   /**< single click (detected on release)   */
	BK_KEY_GESTURE_DOUBLE,      /**< double click                         */
	BK_KEY_GESTURE_LONG,        /**< long press reached threshold         */
	BK_KEY_GESTURE_LONG_UP,     /**< release after a long press           */
	BK_KEY_GESTURE_MAX,
} bk_key_gesture_t;

/** Logical key id, defined by the product (component treats it as opaque). */
typedef uint8_t bk_key_id_t;

/** Hardware source of a key. */
typedef enum {
	BK_KEY_SRC_GPIO = 0,        /**< dedicated GPIO key (multi_button)    */
	BK_KEY_SRC_ADC,             /**< ADC resistor-divider key (adc_key)   */
} bk_key_source_t;

/** Abstract key event delivered to subscribers. */
typedef struct {
	bk_key_id_t      key_id;
	bk_key_gesture_t gesture;
	uint32_t         timestamp_ms;
} bk_key_event_t;

/* Gesture-mask helpers (which gestures to enable for a key). */
#define BK_KEY_GESTURE_BIT(g)   (1u << (g))
#define BK_KEY_GESTURE_ALL      0x0Fu

/**
 * Per-key runtime configuration (replaces the compile-time macro tables used
 * by the dashboard / ai solutions).
 */
typedef struct {
	bk_key_id_t     key_id;         /**< product-defined logical id           */
	bk_key_source_t source;         /**< BK_KEY_SRC_GPIO / BK_KEY_SRC_ADC      */
	uint16_t        gpio_or_chan;   /**< GPIO number (GPIO src) / ADC channel  */
	uint8_t         active_level;   /**< GPIO active level 0/1 (GPIO src only) */
	uint16_t        adc_v_low;      /**< ADC window lower bound mV (ADC only)  */
	uint16_t        adc_v_high;     /**< ADC window upper bound mV (ADC only)  */
	uint16_t        long_press_ms;  /**< long-press threshold, 0 = SDK default */
	uint8_t         gesture_mask;   /**< enabled gestures, see BK_KEY_GESTURE_BIT() */
	bool            wakeup_source;  /**< register the physical pin as wake src */
} bk_key_cfg_t;

/** Subscriber callback type. Runs in the bk_key dispatch thread. */
typedef void (*bk_key_listener_t)(const bk_key_event_t *evt, void *ctx);

/**
 * @brief Initialize the engine with a runtime key table.
 *
 * The table is copied internally, so @p cfgs may live on the stack. Brings up
 * the underlying SDK detector(s) according to each entry's @c source.
 *
 * @return BK_OK on success.
 */
bk_err_t bk_key_init(const bk_key_cfg_t *cfgs, uint8_t num);

/**
 * @brief Subscribe a listener. Multiple listeners are broadcast in
 *        registration order (up to CONFIG_BK_KEY_MAX_LISTENERS).
 */
bk_err_t bk_key_subscribe(bk_key_listener_t cb, void *ctx);

/** @brief Remove a previously subscribed listener. */
bk_err_t bk_key_unsubscribe(bk_key_listener_t cb);

/**
 * @brief Register every key flagged wakeup_source=true as a low-power wake
 *        source on its physical pin (falling edge for active-low keys).
 *
 * For ADC-ladder keys only the "pull-to-ground" (≈0V) key produces a usable
 * GPIO edge; higher-voltage keys cannot wake from deep sleep.
 */
bk_err_t bk_key_register_wakeup(void);

/** @brief Tear down the engine and underlying detectors. */
void bk_key_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK_KEY_H__ */
