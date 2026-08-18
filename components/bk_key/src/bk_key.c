#include <common/sys_config.h>
#include <components/log.h>
#include <os/os.h>
#include <os/mem.h>
#include <string.h>

#include "bk_key.h"
#include "bk_key_internal.h"

#if CONFIG_GPIO_WAKEUP_SUPPORT
#include <driver/gpio.h>
#include "gpio_driver.h"
#endif

#if CONFIG_BK_KEY

#define TAG BK_KEY_TAG
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#ifndef CONFIG_BK_KEY_MAX_LISTENERS
#define CONFIG_BK_KEY_MAX_LISTENERS 4
#endif
#ifndef CONFIG_BK_KEY_THREAD_PRIO
#define CONFIG_BK_KEY_THREAD_PRIO 4
#endif
#ifndef CONFIG_BK_KEY_THREAD_STACK
#define CONFIG_BK_KEY_THREAD_STACK 2048
#endif

#define BK_KEY_QUEUE_NAME  "bk_key_q"
#define BK_KEY_QUEUE_DEPTH 10
#define BK_KEY_THREAD_NAME "bk_key_thr"

typedef struct {
	bk_key_listener_t cb;
	void             *ctx;
} bk_key_sub_t;

static bk_key_cfg_t   *s_cfgs = NULL;
static uint8_t         s_num  = 0;
static bool            s_inited = false;

static beken_queue_t   s_queue  = NULL;
static beken_thread_t  s_thread = NULL;
static beken_mutex_t   s_lock   = NULL;
static bk_key_sub_t    s_subs[CONFIG_BK_KEY_MAX_LISTENERS];

static const bk_key_cfg_t *find_cfg(bk_key_id_t key_id)
{
	for (uint8_t i = 0; i < s_num; i++) {
		if (s_cfgs[i].key_id == key_id) {
			return &s_cfgs[i];
		}
	}
	return NULL;
}

const bk_key_cfg_t *bk_key_cfgs(uint8_t *num_out)
{
	if (num_out) {
		*num_out = s_num;
	}
	return s_cfgs;
}

void bk_key_post_event(bk_key_id_t key_id, bk_key_gesture_t gesture)
{
	if (!s_inited || s_queue == NULL) {
		return;
	}
	if (gesture >= BK_KEY_GESTURE_MAX) {
		return;
	}

	const bk_key_cfg_t *cfg = find_cfg(key_id);
	if (cfg == NULL) {
		LOGW("drop event: unknown key_id=%d\r\n", key_id);
		return;
	}
	if (!(cfg->gesture_mask & BK_KEY_GESTURE_BIT(gesture))) {
		/* gesture disabled for this key -> ignore quietly */
		return;
	}

	bk_key_event_t evt = {
		.key_id       = key_id,
		.gesture      = gesture,
		.timestamp_ms = rtos_get_time(),
	};

	/* 1000ms timeout: detector contexts must never block forever. */
	if (rtos_push_to_queue(&s_queue, &evt, 1000) != kNoErr) {
		LOGW("queue full, drop key=%d gesture=%d\r\n", key_id, gesture);
	}
}

static void bk_key_broadcast(const bk_key_event_t *evt)
{
	bk_key_sub_t local[CONFIG_BK_KEY_MAX_LISTENERS];

	/* Snapshot subscribers under lock, then invoke without holding it so a
	 * listener may (un)subscribe safely and cannot deadlock the engine. */
	rtos_lock_mutex(&s_lock);
	memcpy(local, s_subs, sizeof(local));
	rtos_unlock_mutex(&s_lock);

	for (int i = 0; i < CONFIG_BK_KEY_MAX_LISTENERS; i++) {
		if (local[i].cb) {
			local[i].cb(evt, local[i].ctx);
		}
	}
}

static void bk_key_thread(void *param)
{
	bk_key_event_t evt;

	while (1) {
		if (rtos_pop_from_queue(&s_queue, &evt, BEKEN_WAIT_FOREVER) == kNoErr) {
			LOGD("dispatch key=%d gesture=%d t=%u\r\n",
			     evt.key_id, evt.gesture, evt.timestamp_ms);
			bk_key_broadcast(&evt);
		}
	}
}

bk_err_t bk_key_subscribe(bk_key_listener_t cb, void *ctx)
{
	if (cb == NULL) {
		return BK_ERR_PARAM;
	}
	if (!s_inited) {
		return BK_ERR_NOT_INIT;
	}

	bk_err_t ret = BK_ERR_NO_MEM;
	rtos_lock_mutex(&s_lock);
	for (int i = 0; i < CONFIG_BK_KEY_MAX_LISTENERS; i++) {
		if (s_subs[i].cb == cb) {  /* already subscribed */
			ret = BK_OK;
			break;
		}
		if (s_subs[i].cb == NULL) {
			s_subs[i].cb  = cb;
			s_subs[i].ctx = ctx;
			ret = BK_OK;
			break;
		}
	}
	rtos_unlock_mutex(&s_lock);

	if (ret != BK_OK) {
		LOGE("subscriber table full (max=%d)\r\n", CONFIG_BK_KEY_MAX_LISTENERS);
	}
	return ret;
}

bk_err_t bk_key_unsubscribe(bk_key_listener_t cb)
{
	if (cb == NULL || !s_inited) {
		return BK_ERR_PARAM;
	}

	rtos_lock_mutex(&s_lock);
	for (int i = 0; i < CONFIG_BK_KEY_MAX_LISTENERS; i++) {
		if (s_subs[i].cb == cb) {
			s_subs[i].cb  = NULL;
			s_subs[i].ctx = NULL;
		}
	}
	rtos_unlock_mutex(&s_lock);
	return BK_OK;
}

bk_err_t bk_key_register_wakeup(void)
{
#if CONFIG_GPIO_WAKEUP_SUPPORT
	if (!s_inited) {
		return BK_ERR_NOT_INIT;
	}

	for (uint8_t i = 0; i < s_num; i++) {
		const bk_key_cfg_t *c = &s_cfgs[i];
		if (!c->wakeup_source) {
			continue;
		}

		uint32_t gpio_id;
		if (c->source == BK_KEY_SRC_ADC) {
#if CONFIG_BK_KEY_ADC
			/* ADC-ladder keys all share the ADC input pin. Only the
			 * pull-to-ground (~0V) key yields a usable falling edge. */
			gpio_id = CONFIG_ADC_KEY2_GPIO;
#else
			continue;
#endif
		} else {
			gpio_id = c->gpio_or_chan;
		}

		/* active-low key pressed -> falling edge; active-high -> rising. */
		gpio_int_type_t edge = (c->active_level == 0) ?
		                       GPIO_INT_TYPE_FALLING_EDGE :
		                       GPIO_INT_TYPE_RISING_EDGE;
		bk_gpio_register_wakeup_source((gpio_id_t)gpio_id, edge);
		LOGI("register wakeup src gpio=%u edge=%d (key_id=%d)\r\n",
		     gpio_id, edge, c->key_id);
	}
	return BK_OK;
#else
	LOGW("GPIO wakeup not supported (CONFIG_GPIO_WAKEUP_SUPPORT off)\r\n");
	return BK_ERR_NOT_SUPPORT;
#endif
}

bk_err_t bk_key_init(const bk_key_cfg_t *cfgs, uint8_t num)
{
	bk_err_t ret;

	if (s_inited) {
		LOGW("already inited\r\n");
		return BK_OK;
	}
	if (cfgs == NULL || num == 0) {
		return BK_ERR_PARAM;
	}

	s_cfgs = (bk_key_cfg_t *)os_malloc(sizeof(bk_key_cfg_t) * num);
	if (s_cfgs == NULL) {
		return BK_ERR_NO_MEM;
	}
	memcpy(s_cfgs, cfgs, sizeof(bk_key_cfg_t) * num);
	s_num = num;
	memset(s_subs, 0, sizeof(s_subs));

	ret = rtos_init_mutex(&s_lock);
	if (ret != kNoErr) {
		LOGE("init mutex fail %d\r\n", ret);
		goto err_free;
	}

	ret = rtos_init_queue(&s_queue, BK_KEY_QUEUE_NAME,
	                      sizeof(bk_key_event_t), BK_KEY_QUEUE_DEPTH);
	if (ret != kNoErr) {
		LOGE("init queue fail %d\r\n", ret);
		goto err_mutex;
	}

	ret = rtos_create_thread(&s_thread, CONFIG_BK_KEY_THREAD_PRIO,
	                         BK_KEY_THREAD_NAME, bk_key_thread,
	                         CONFIG_BK_KEY_THREAD_STACK, NULL);
	if (ret != kNoErr) {
		LOGE("create thread fail %d\r\n", ret);
		goto err_queue;
	}

	s_inited = true;

	ret = bk_key_driver_start(s_cfgs, s_num);
	if (ret != BK_OK) {
		LOGE("driver start fail %d\r\n", ret);
		/* engine still usable; detectors just won't feed events */
	}

	LOGI("bk_key init done, %d keys\r\n", s_num);
	return BK_OK;

err_queue:
	rtos_deinit_queue(&s_queue);
	s_queue = NULL;
err_mutex:
	rtos_deinit_mutex(&s_lock);
	s_lock = NULL;
err_free:
	os_free(s_cfgs);
	s_cfgs = NULL;
	s_num = 0;
	return ret;
}

void bk_key_deinit(void)
{
	if (!s_inited) {
		return;
	}
	s_inited = false;

	bk_key_driver_stop();

	if (s_thread) {
		rtos_delete_thread(&s_thread);
		s_thread = NULL;
	}
	if (s_queue) {
		rtos_deinit_queue(&s_queue);
		s_queue = NULL;
	}
	if (s_lock) {
		rtos_deinit_mutex(&s_lock);
		s_lock = NULL;
	}
	if (s_cfgs) {
		os_free(s_cfgs);
		s_cfgs = NULL;
	}
	s_num = 0;
}

#endif /* CONFIG_BK_KEY */
