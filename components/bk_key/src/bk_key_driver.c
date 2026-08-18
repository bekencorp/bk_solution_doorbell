#include <common/sys_config.h>
#include <components/log.h>
#include <os/os.h>
#include <string.h>

#include "bk_key.h"
#include "bk_key_internal.h"

#include <key_adapter.h>   /* SDK 'key' component: KeyConfig_t, key_handler_t, ... */

#if CONFIG_BK_KEY_ADC
#include "adc_key_main.h"  /* SDK 'adc_key' component: ADCKEY_S, bk_adc*_* ...     */
#endif

#if CONFIG_BK_KEY

#define TAG BK_KEY_TAG
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* ======================== ADC keys (resistor ladder) ======================== */
/*
 * One ADC channel, N voltage windows. The SDK adc_key engine keeps a linked
 * list of detector items, each with its own [low,high] window + callbacks, so
 * N windows on a single line is fully supported (S4/S5 is only how the SDK demo
 * names two of them). We carry our logical key_id in ADCKEY_S.user_data and
 * recover it in the shared gesture callbacks.
 */
#if CONFIG_BK_KEY_ADC

static bool s_adc_started = false;
static adc_chan_t s_adc_chan = ADC_MAX;

#if CONFIG_BK_KEY_ADC_CALIB
/*
 * TEMPORARY calibration helper. Reads the shared ADC channel and logs the
 * voltage on every meaningful change so the real per-key windows can be
 * measured on a physical board. Disable CONFIG_BK_KEY_ADC_CALIB for production.
 */
#define BK_KEY_CALIB_PERIOD_MS   50
#define BK_KEY_CALIB_DELTA_MV    80
#define BK_KEY_CALIB_HEARTBEAT   40   /* ~2s idle heartbeat */

static beken_thread_t s_calib_thread = NULL;
static bool           s_calib_run = false;

static void bk_key_calib_thread(void *param)
{
	uint32_t last = 0xFFFFFFFF;
	uint32_t hb = 0;

	LOGI("[CALIB] started on ADC ch%d. Press K3..K6 and read 'v=..mV'.\r\n",
	     s_adc_chan);

	while (s_calib_run) {
		uint32_t v = adc_key_get_gpio_voltage(s_adc_chan);
		if (v != 9999) {   /* 9999 == sample error, skip */
			uint32_t diff = (v > last) ? (v - last) : (last - v);
			if (last == 0xFFFFFFFF || diff >= BK_KEY_CALIB_DELTA_MV) {
				LOGI("[CALIB] v=%umV\r\n", v);
				last = v;
				hb = 0;
			} else if (++hb >= BK_KEY_CALIB_HEARTBEAT) {
				LOGI("[CALIB] idle v=%umV\r\n", v);
				hb = 0;
			}
		}
		rtos_delay_milliseconds(BK_KEY_CALIB_PERIOD_MS);
	}
	s_calib_thread = NULL;
	rtos_delete_thread(NULL);
}

static void bk_key_calib_start(void)
{
	if (s_calib_thread) {
		return;
	}
	s_calib_run = true;
	if (rtos_create_thread(&s_calib_thread, 5, "bk_key_calib",
	                       bk_key_calib_thread, 2048, NULL) != kNoErr) {
		LOGE("[CALIB] thread create failed\r\n");
		s_calib_run = false;
	}
}

static void bk_key_calib_stop(void)
{
	s_calib_run = false;   /* thread self-deletes on next loop */
}
#endif /* CONFIG_BK_KEY_ADC_CALIB */

static inline bk_key_id_t adc_key_id_of(void *param)
{
	ADCKEY_S *h = (ADCKEY_S *)param;
	return (bk_key_id_t)(uint32_t)h->user_data;
}

static void adc_short_cb(void *param)
{
	bk_key_post_event(adc_key_id_of(param), BK_KEY_GESTURE_SHORT);
}
static void adc_double_cb(void *param)
{
	bk_key_post_event(adc_key_id_of(param), BK_KEY_GESTURE_DOUBLE);
}
static void adc_long_cb(void *param)
{
	bk_key_post_event(adc_key_id_of(param), BK_KEY_GESTURE_LONG);
}

static bk_err_t adc_keys_start(const bk_key_cfg_t *cfgs, uint8_t num)
{
	int adc_cnt = 0;
	adc_chan_t chan = (adc_chan_t)CONFIG_ADC_KEY2_ADC_CHAN;

	for (uint8_t i = 0; i < num; i++) {
		if (cfgs[i].source == BK_KEY_SRC_ADC) {
			chan = (adc_chan_t)cfgs[i].gpio_or_chan; /* all ADC keys share it */
			adc_cnt++;
		}
	}
	if (adc_cnt == 0) {
		return BK_OK;
	}

	/* gpio arg is only used for logging by the SDK; the ADC read uses chan. */
	bk_adc_key_init((gpio_id_t)CONFIG_ADC_KEY2_GPIO, chan);
	s_adc_chan = chan;
	s_adc_started = true;

	for (uint8_t i = 0; i < num; i++) {
		if (cfgs[i].source != BK_KEY_SRC_ADC) {
			continue;
		}
		adckey_configure_t c = {
			.lowest_level   = cfgs[i].adc_v_low,
			.highest_level  = cfgs[i].adc_v_high,
			.user_index     = (ADCKEY_INDEX)cfgs[i].key_id,
			.short_press_cb  = adc_short_cb,
			.double_press_cb = adc_double_cb,
			.long_press_cb   = adc_long_cb,
			.hold_press_cb   = NULL,
		};
		if (bk_adckey_item_configure(&c) != kNoErr) {
			LOGE("adc item cfg fail key_id=%d [%d,%d]mV\r\n",
			     cfgs[i].key_id, cfgs[i].adc_v_low, cfgs[i].adc_v_high);
		} else {
			LOGI("adc key_id=%d window=[%d,%d]mV\r\n",
			     cfgs[i].key_id, cfgs[i].adc_v_low, cfgs[i].adc_v_high);
		}
	}
	/* Note: ADC long-press threshold is global (CONFIG_ADC_KEY_LONG_PRESS_MS);
	 * per-key long_press_ms is not honored on the ADC path. */
#if CONFIG_BK_KEY_ADC_CALIB
	bk_key_calib_start();
#endif
	return BK_OK;
}

static void adc_keys_stop(void)
{
	if (s_adc_started) {
#if CONFIG_BK_KEY_ADC_CALIB
		bk_key_calib_stop();
#endif
		bk_adc_key_deinit();
		s_adc_started = false;
	}
}

#endif /* CONFIG_BK_KEY_ADC */

/* ======================== GPIO keys (multi_button) ======================== */
/*
 * Dedicated GPIO keys via the SDK 'key' component. Each (key, gesture) is
 * assigned a private SDK user-event id (100..); the SDK handler translates it
 * back to (key_id, gesture) and forwards to the engine. Kept generic so a
 * future pure-GPIO board reuses the same component unchanged.
 */
#define BK_KEY_MAX_GPIO 8

static KeyConfig_t s_gpio_kc[BK_KEY_MAX_GPIO];
static uint8_t     s_gpio_n = 0;

typedef struct {
	uint8_t          ev;
	bk_key_id_t      key_id;
	bk_key_gesture_t gesture;
} gpio_evt_map_t;

static gpio_evt_map_t s_gpio_map[BK_KEY_MAX_GPIO * BK_KEY_GESTURE_MAX];
static uint8_t        s_gpio_map_n = 0;

static void gpio_evt_handler(uint8_t event)
{
	for (uint8_t i = 0; i < s_gpio_map_n; i++) {
		if (s_gpio_map[i].ev == event) {
			bk_key_post_event(s_gpio_map[i].key_id, s_gpio_map[i].gesture);
			return;
		}
	}
}

static uint8_t map_add(uint8_t ev, bk_key_id_t key_id, bk_key_gesture_t g)
{
	if (s_gpio_map_n < (uint8_t)(sizeof(s_gpio_map) / sizeof(s_gpio_map[0]))) {
		s_gpio_map[s_gpio_map_n].ev      = ev;
		s_gpio_map[s_gpio_map_n].key_id  = key_id;
		s_gpio_map[s_gpio_map_n].gesture = g;
		s_gpio_map_n++;
	}
	return ev;
}

static bk_err_t gpio_keys_start(const bk_key_cfg_t *cfgs, uint8_t num)
{
	s_gpio_n = 0;
	s_gpio_map_n = 0;

	for (uint8_t i = 0; i < num; i++) {
		if (cfgs[i].source != BK_KEY_SRC_GPIO) {
			continue;
		}
		if (s_gpio_n >= BK_KEY_MAX_GPIO) {
			LOGE("too many GPIO keys (max %d)\r\n", BK_KEY_MAX_GPIO);
			break;
		}

		/* Private event-id block for this key: base .. base+3.
		 * base stays within the SDK user range (100..255). */
		uint8_t base = (uint8_t)USER_EVENT_START + s_gpio_n * BK_KEY_GESTURE_MAX;
		uint8_t mask = cfgs[i].gesture_mask;

		KeyConfig_t *kc = &s_gpio_kc[s_gpio_n];
		memset(kc, 0, sizeof(*kc));
		kc->gpio_id      = (uint8_t)cfgs[i].gpio_or_chan;
		kc->active_level = cfgs[i].active_level;
		kc->short_event  = (mask & BK_KEY_GESTURE_BIT(BK_KEY_GESTURE_SHORT)) ?
		    map_add(base + BK_KEY_GESTURE_SHORT, cfgs[i].key_id, BK_KEY_GESTURE_SHORT) : EVENT_NONE;
		kc->double_event = (mask & BK_KEY_GESTURE_BIT(BK_KEY_GESTURE_DOUBLE)) ?
		    map_add(base + BK_KEY_GESTURE_DOUBLE, cfgs[i].key_id, BK_KEY_GESTURE_DOUBLE) : EVENT_NONE;
		kc->long_event   = (mask & BK_KEY_GESTURE_BIT(BK_KEY_GESTURE_LONG)) ?
		    map_add(base + BK_KEY_GESTURE_LONG, cfgs[i].key_id, BK_KEY_GESTURE_LONG) : EVENT_NONE;
		kc->long_press_up_event = (mask & BK_KEY_GESTURE_BIT(BK_KEY_GESTURE_LONG_UP)) ?
		    map_add(base + BK_KEY_GESTURE_LONG_UP, cfgs[i].key_id, BK_KEY_GESTURE_LONG_UP) : EVENT_NONE;

		s_gpio_n++;
	}

	if (s_gpio_n == 0) {
		return BK_OK;
	}

	bk_key_register_event_handler(gpio_evt_handler);
	bk_key_driver_init(s_gpio_kc, s_gpio_n);   /* SDK keeps the pointer -> static */
	LOGI("gpio keys started: %d\r\n", s_gpio_n);
	return BK_OK;
}

static void gpio_keys_stop(void)
{
	if (s_gpio_n) {
		bk_key_driver_deinit(s_gpio_kc, s_gpio_n);
		s_gpio_n = 0;
		s_gpio_map_n = 0;
	}
}

/* ======================== driver facade ======================== */

bk_err_t bk_key_driver_start(const bk_key_cfg_t *cfgs, uint8_t num)
{
	bk_err_t ret = BK_OK;

	gpio_keys_start(cfgs, num);

#if CONFIG_BK_KEY_ADC
	ret = adc_keys_start(cfgs, num);
#else
	for (uint8_t i = 0; i < num; i++) {
		if (cfgs[i].source == BK_KEY_SRC_ADC) {
			LOGW("ADC key_id=%d ignored: CONFIG_BK_KEY_ADC is off\r\n",
			     cfgs[i].key_id);
		}
	}
#endif
	return ret;
}

void bk_key_driver_stop(void)
{
#if CONFIG_BK_KEY_ADC
	adc_keys_stop();
#endif
	gpio_keys_stop();
}

#endif /* CONFIG_BK_KEY */
