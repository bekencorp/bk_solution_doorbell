#include <common/sys_config.h>
#include <components/log.h>
#include <components/system.h>
#include <os/os.h>

#include "key_map.h"

#if CONFIG_BK_KEY

#include "bk_key.h"
#include "bk_factory_config.h"
#include "doorbell_netcfg.h"
#include "ui/doorbell_ui.h"

#define TAG "key_map"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

/*
 * Board: BK7259 QFN128 EVB V1.0.
 * K3-K6 are an ADC resistor-ladder on P25 / ADC channel 1 (4 keys share the
 * line). Silk K3-K6 == net KEY1-KEY4 (confirmed).
 *
 * Measured on-board (CP sampler scale; idle floats high, keys pull to taps):
 *   idle (no key) = 9594 mV
 *   K3 = KEY1/WAKE UP  =    0 mV   (also the deep-sleep wake key)
 *   K4 = KEY2          = 2075 mV
 *   K5 = KEY3          = 3955 mV
 *   K6 = KEY4          = 5952 mV
 * Windows below are placed at the mid-points between adjacent taps, all well
 * under the 9594 mV idle baseline so idle never matches. Re-measure with
 * CONFIG_BK_KEY_ADC_CALIB if the board / VBAT changes.
 */
enum {
	DKEY_K3 = 1,   /* short -> clear netcfg + enter provisioning (also deep-sleep wake key) */
	DKEY_K4,       /* short -> UI settings page                                             */
	DKEY_K5,       /* short -> UI back to home                                              */
	DKEY_K6,       /* short -> erase netcfg; long -> factory reset (all config + reboot)    */
};

#define ADC1_CHAN   1   /* P25 == SARADC channel 1 */

static const bk_key_cfg_t s_doorbell_keys[] = {
	{ .key_id = DKEY_K3, .source = BK_KEY_SRC_ADC, .gpio_or_chan = ADC1_CHAN,
	  .adc_v_low = 0,    .adc_v_high = 1000, .long_press_ms = 3000,   /* ~0mV   */
	  .gesture_mask = BK_KEY_GESTURE_BIT(BK_KEY_GESTURE_SHORT),
	  .wakeup_source = true },
	{ .key_id = DKEY_K4, .source = BK_KEY_SRC_ADC, .gpio_or_chan = ADC1_CHAN,
	  .adc_v_low = 1000, .adc_v_high = 3000, .long_press_ms = 3000,   /* ~2075mV */
	  .gesture_mask = BK_KEY_GESTURE_BIT(BK_KEY_GESTURE_SHORT) },
	{ .key_id = DKEY_K5, .source = BK_KEY_SRC_ADC, .gpio_or_chan = ADC1_CHAN,
	  .adc_v_low = 3000, .adc_v_high = 4950, .long_press_ms = 3000,   /* ~3955mV */
	  .gesture_mask = BK_KEY_GESTURE_ALL },
	{ .key_id = DKEY_K6, .source = BK_KEY_SRC_ADC, .gpio_or_chan = ADC1_CHAN,
	  .adc_v_low = 4950, .adc_v_high = 7700, .long_press_ms = 3000,   /* ~5952mV */
	  .gesture_mask = BK_KEY_GESTURE_BIT(BK_KEY_GESTURE_SHORT)
	                | BK_KEY_GESTURE_BIT(BK_KEY_GESTURE_LONG) },
};

/*
 * K6 长按：回归出厂设置。清空所有已保存配置后重启：
 *   1) doorbell_netcfg_erase() 清除配网信息（重连凭据）并断开当前 STA；
 *   2) bk_factory_reset()      把出厂配置（音量/语言/用户项）恢复为默认值；
 *   3) 延时片刻等 flash 写入与日志刷完，再 bk_reboot() 重启到出厂初始状态。
 */
static void doorbell_factory_reset(void)
{
	LOGW("factory reset: erase netcfg + factory config, then reboot\r\n");
	doorbell_netcfg_erase();
	bk_factory_reset();
	rtos_delay_milliseconds(200);
	bk_reboot();
}

static void doorbell_key_cb(const bk_key_event_t *e, void *ctx)
{
	(void)ctx;
	LOGI("key event: id=%d gesture=%d\r\n", e->key_id, e->gesture);

	switch (e->key_id) {
	case DKEY_K3:   /* single-click -> clear netcfg then re-provision */
		if (e->gesture == BK_KEY_GESTURE_SHORT) {
			/* 点击"配网"：先清除配网信息，再切到配网页启动 BLE 配网。 */
			doorbell_ui_request_provisioning();
		}
		break;
	case DKEY_K4:   /* single-click -> UI settings page */
		if (e->gesture == BK_KEY_GESTURE_SHORT) {
			doorbell_ui_goto(DB_UI_PAGE_SETTINGS);
		}
		break;
	case DKEY_K5:   /* single-click -> UI back to home */
		if (e->gesture == BK_KEY_GESTURE_SHORT) {
			doorbell_ui_goto(DB_UI_PAGE_HOME);
		}
		break;
	case DKEY_K6:   /* short -> erase netcfg; long -> factory reset */
		if (e->gesture == BK_KEY_GESTURE_SHORT) {
			LOGW("erase provisioning info via K6 short press\r\n");
			doorbell_netcfg_erase();
		} else if (e->gesture == BK_KEY_GESTURE_LONG) {
			doorbell_factory_reset();
		}
		break;
	default:
		break;
	}
}

void doorbell_key_start(void)
{
	bk_err_t ret = bk_key_init(s_doorbell_keys,
	                           sizeof(s_doorbell_keys) / sizeof(s_doorbell_keys[0]));
	if (ret != BK_OK) {
		LOGW("bk_key_init failed: %d\r\n", ret);
		return;
	}
	bk_key_subscribe(doorbell_key_cb, NULL);
	bk_key_register_wakeup();   /* only K3 (wakeup_source=true) is registered */
	LOGI("doorbell keys started (K3-K6 on ADC ch%d)\r\n", ADC1_CHAN);
}

#else /* !CONFIG_BK_KEY */

void doorbell_key_start(void)
{
}

#endif /* CONFIG_BK_KEY */
