// Copyright 2020-2021 Beken
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

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <common/bk_err.h>
#include <components/log.h>
#include <avdk_error.h>
#include <avdk_check.h>
#include <modules/pm.h>

#include "components/bk_frame_buffer.h"
#include <components/bk_display.h>          /* umbrella: bus + panel + display ctlr */
#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include "gpio_driver.h"
#include "app_display.h"
#if CONFIG_LCD_LT8912B_MIPI_BRIDGE
#include <lcd/lcd_mipi_lt8912b_bridge.h>     /* bridge-private I2C pin setter */
#endif

#define TAG "app-disp"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

typedef enum
{
    APP_DISPLAY_STATE_OFF = 0,
    APP_DISPLAY_STATE_TURNING_ON,
    APP_DISPLAY_STATE_ON,
    APP_DISPLAY_STATE_TURNING_OFF,
} app_display_state_t;

/**
 * @brief Live display context.
 *
 * Owns every bk_display object the bring-up sequence creates. Pointers
 * are listed in acquisition order; ::app_display_teardown() releases
 * them in reverse so partial-build failures roll back exactly the
 * resources that were actually claimed.
 */
typedef struct
{
    app_display_state_t state;
#if (DISP_DEBUG_TIMER_ENABLE)
    beken_timer_t timer;
    float rps;             //hw refreash per second
    float fps;             //frame per second
    uint32_t refreash_count;
    uint32_t frame_count;
    uint32_t last_refreash_count;
    uint32_t last_frame_count;
#endif
    bk_display_bus_handle_t    bus;
    bk_avdk_lcd_panel_handle_t panel;
    bk_display_ctlr_handle_t   ctlr;
    bool ctlr_inited;          /*!< true after bk_display_init() succeeds */
    bool ctlr_opened;          /*!< true after bk_display_open() succeeds */
    int8_t backlight_pin;      /*!< <0 if disabled */
    bool   backlight_driven;   /*!< true once init() drove the pin high */
} display_ctx_t;

/* File-private state. The mutex serialises the multi-step bring-up /
 * tear-down so other TUs can only observe APP_DISPLAY_STATE_ON contexts
 * via the public accessors below. */
static display_ctx_t *s_disp_ctx = NULL;
static beken_mutex_t  s_disp_mutex = NULL;
static display_board_config_t *s_display_board_config = NULL;

static bool app_display_state_is_on(const display_ctx_t *ctx)
{
    return (ctx != NULL) && (ctx->state == APP_DISPLAY_STATE_ON) && (ctx->ctlr != NULL);
}

/**
 * @brief Drive the backlight GPIO high.
 *
 * Must be the very last step of bring-up so the panel is already
 * scanning frames by the time the human sees light.
 */
static void app_display_backlight_on(display_ctx_t *ctx)
{
    if (ctx->backlight_pin < 0) {
        return;
    }
    gpio_id_t pin = (gpio_id_t)ctx->backlight_pin;
    gpio_dev_unmap(pin);
    BK_LOG_ON_ERR(bk_gpio_enable_output(pin));
    BK_LOG_ON_ERR(bk_gpio_pull_up(pin));
    bk_gpio_set_capacity(pin, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_high(pin);
    ctx->backlight_driven = true;
}

static void app_display_backlight_off(display_ctx_t *ctx)
{
    if (ctx->backlight_pin < 0 || !ctx->backlight_driven) {
        return;
    }
    bk_gpio_set_output_low((gpio_id_t)ctx->backlight_pin);
    ctx->backlight_driven = false;
}

/**
 * @brief Release every resource claimed by ::app_display_bringup().
 *
 * Mirrors the bring-up ladder in reverse so a partial-build failure
 * only undoes the steps that actually succeeded.
 */
static void app_display_teardown(display_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    app_display_backlight_off(ctx);

    if (ctx->ctlr) {
        if (ctx->ctlr_opened) {
            (void)bk_display_close(ctx->ctlr);
            ctx->ctlr_opened = false;
        }
        if (ctx->ctlr_inited) {
            (void)bk_display_deinit(ctx->ctlr);
            ctx->ctlr_inited = false;
        }
        (void)bk_display_delete(ctx->ctlr);
        ctx->ctlr = NULL;
    }

    if (ctx->panel) {
        (void)bk_lcd_panel_delete(ctx->panel);
        ctx->panel = NULL;
    }

    if (ctx->bus) {
        (void)bk_display_bus_delete(ctx->bus);
        ctx->bus = NULL;
    }
}

static void app_display_ctx_destroy(display_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    app_display_teardown(ctx);
    ctx->state = APP_DISPLAY_STATE_OFF;
    os_memset(ctx, 0, sizeof(display_ctx_t));
    os_free(ctx);
}

/* INIT_ONCE-style first-time mutex creation. Two callers racing into
 * app_display_lock() before any explicit module-init point should still
 * end up with exactly one mutex - we double-check around a brief
 * critical section so the heap allocation inside rtos_init_mutex
 * happens at most once. */
static bk_err_t app_display_lock_init_once(void)
{
    if (s_disp_mutex != NULL) {
        return BK_OK;
    }

    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    bk_err_t ret = BK_OK;
    if (s_disp_mutex == NULL) {
        ret = rtos_init_mutex(&s_disp_mutex);
    }
    GLOBAL_INT_RESTORE();
    return ret;
}

static bk_err_t app_display_lock(void)
{
    bk_err_t ret = app_display_lock_init_once();
    if (ret != BK_OK) {
        LOGE("%s, init mutex failed: %d\n", __func__, ret);
        return ret;
    }

    ret = rtos_lock_mutex(&s_disp_mutex);
    if (ret != BK_OK) {
        LOGE("%s, lock mutex failed: %d\n", __func__, ret);
    }
    return ret;
}

static void app_display_unlock(void)
{
    if (s_disp_mutex != NULL) {
        (void)rtos_unlock_mutex(&s_disp_mutex);
    }
}

/**
 * @brief Vote display-domain AuxLDO (1.8V VDDIO) on/off.
 *
 * Single owner of ::PM_AUXLDO_USER_DISPLAY. Called by
 * ::app_mipi_lcd_turn_on() / ::app_mipi_lcd_turn_off(); higher layers
 * MUST NOT vote ::PM_AUXLDO_USER_DISPLAY themselves.
 */
avdk_err_t app_display_power_enable(bool enable)
{
    int ldo_en = enable ? PM_AUXLDO_ENABLE : PM_AUXLDO_DISABLE;
    LOGI("%s, vddio enable: %d\n", __func__, ldo_en);

    pm_auxldo_ctrl_cfg_t auxldo_cfg = {0};
    auxldo_cfg.ldo   = AUXLDOS_SEL_1P8V;
    auxldo_cfg.out   = PM_AUXLDO_1P8V_OUT_1P8V;
    auxldo_cfg.user  = PM_AUXLDO_USER_DISPLAY;
    auxldo_cfg.state = ldo_en;
    AVDK_RETURN_ON_ERROR(bk_pm_auxldo_ctrl_vote(&auxldo_cfg), TAG, "display 1p8v ldo vote failed");
    rtos_delay_milliseconds(1);
    return AVDK_ERR_OK;
}

void *app_mipi_lcd_handle_get(void)
{
    void *handle = NULL;

    if (app_display_lock() != BK_OK) {
        return NULL;
    }

    if (app_display_state_is_on(s_disp_ctx)) {
        handle = s_disp_ctx->ctlr;
    }

    app_display_unlock();
    return handle;
}


int app_mipi_lcd_turn_off(void)
{
    bk_err_t ret = BK_OK;
    display_ctx_t *ctx = NULL;

    if (app_display_lock() != BK_OK) {
        return BK_FAIL;
    }

    ctx = s_disp_ctx;
    if (ctx == NULL) {
        LOGI("%s, already turn off %d \n", __func__, __LINE__);
        app_display_unlock();
        return ret;
    }

    if (ctx->state != APP_DISPLAY_STATE_ON) {
        LOGE("%s, invalid display state: %d\n", __func__, ctx->state);
        app_display_unlock();
        return BK_FAIL;
    }

    ctx->state = APP_DISPLAY_STATE_TURNING_OFF;

    s_disp_ctx = NULL;
    app_display_ctx_destroy(ctx);
    app_display_unlock();

    /* VDDIO must drop AFTER the panel + DPU have been torn down so the
     * panel never sees a floating data line above its supply. */
    (void)app_display_power_enable(false);
    LOGI("%s complete\n", __func__);
    return BK_OK;
}


int app_mipi_lcd_turn_on(display_board_config_t *config)
{
    int ret = BK_OK;
    display_ctx_t *ctx = NULL;
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");

    if (config->mipi.panel == NULL) {
        LOGE("No panel specified and no default panel config\n");
        return BK_FAIL;
    }

    if (app_display_lock() != BK_OK) {
        return BK_FAIL;
    }

    if (s_disp_ctx) {
        LOGW("%s already turned on\n", __func__);
        if (s_disp_ctx->state != APP_DISPLAY_STATE_ON) {
            LOGE("%s, display state is invalid: %d\n", __func__, s_disp_ctx->state);
            ret = BK_FAIL;
        }
        app_display_unlock();
        return ret;
    }

    ctx = (display_ctx_t *)os_malloc(sizeof(display_ctx_t));
    if (ctx == NULL) {
        LOGE("malloc s_disp_ctx NULL \n");
        app_display_unlock();
        return BK_ERR_NO_MEM;
    }
    os_memset(ctx, 0, sizeof(*ctx));
    ctx->state = APP_DISPLAY_STATE_TURNING_ON;
    ctx->backlight_pin = config->mipi.enable ? config->mipi.pin_backlight : -1;

    /* 1. Panel-rail VDDIO must be live before the bridge / panel sees
     *    any I2C / DSI traffic. */
    ret = app_display_power_enable(true);
    if (ret != AVDK_ERR_OK) {
        LOGE("vddio on err: %d\n", ret);
        goto err;
    }

#if CONFIG_LCD_LT8912B_MIPI_BRIDGE
    /* LT8912B HDMI bridge needs its private SW-I2C pins forwarded so the
     * bridge IC can be configured before DSI traffic starts. */
    if (config->mipi.panel == &lcd_device_lt8912b_mipi) {
        bk_lcd_lt8912b_io_pins_t pins = {
            .scl_pin = config->mipi.pin_scl,
            .sda_pin = config->mipi.pin_sda,
        };
        BK_LOG_ON_ERR(bk_lcd_lt8912b_set_io_pins(&pins));
    }
#endif

    /* 2. DSI bus. */
    ret = bk_display_dsi_bus_new(&ctx->bus, NULL);
    if (ret != AVDK_ERR_OK) {
        LOGE("dsi bus new err: %d\n", ret);
        goto err;
    }
    bk_lcd_panel_config_t panel_dev_cfg = {
        .reset_pin = config->mipi.pin_reset,
    };
    ret = bk_lcd_mipi_panel_new(ctx->bus, &panel_dev_cfg,
                                config->mipi.panel, &ctx->panel);
    if (ret != AVDK_ERR_OK) {
        LOGE("mipi panel new err: %d\n", ret);
        goto err;
    }

    bk_display_dpu_config_t dpu_cfg = {
        .video.enable      = config->dpu_video.enable,
        .video.decompress  = config->dpu_video.decompress,
        .video.format      = config->dpu_video.format,
    };
    ret = bk_display_dpu_ctlr_new(&ctx->ctlr, ctx->panel, &dpu_cfg);
    if (ret != AVDK_ERR_OK) {
        LOGE("dpu ctlr new err: %d\n", ret);
        goto err;
    }
    ret = bk_display_init(ctx->ctlr);
    if (ret != AVDK_ERR_OK) {
        LOGE("display init err: %d\n", ret);
        goto err;
    }
    ctx->ctlr_inited = true;
    ret = bk_display_open(ctx->ctlr);
    if (ret != AVDK_ERR_OK) {
        LOGE("display open err: %d\n", ret);
        goto err;
    }
    ctx->ctlr_opened = true;

    /* 5. Backlight last so the panel never shows garbage. */
    app_display_backlight_on(ctx);

    LOGI("bring-up complete: panel='%s'\n",
         config->mipi.panel->name ? config->mipi.panel->name : "<unnamed>");

    ctx->state = APP_DISPLAY_STATE_ON;
    s_disp_ctx = ctx;
    app_display_unlock();
    return BK_OK;

err:
    if (ctx != NULL) {
        ctx->state = APP_DISPLAY_STATE_OFF;
        app_display_ctx_destroy(ctx);
        s_disp_ctx = NULL;
    }
    (void)app_display_power_enable(false);
    app_display_unlock();
    LOGE("%s fail\n", __func__);
    return ret;
}

int app_mipi_lcd_flush(void *frame, avdk_err_t (*free_t)(void *args))
{
    bk_err_t ret = AVDK_ERR_GENERIC;

    if (app_display_lock() != BK_OK) {
        return ret;
    }

    if (app_display_state_is_on(s_disp_ctx)) {
        ret = bk_display_flush(s_disp_ctx->ctlr, frame, free_t);
    }

    app_display_unlock();
    return ret;
}

bool app_mipi_lcd_state_get(void)
{
    bool enabled = false;

    if (app_display_lock() != BK_OK) {
        return false;
    }

    if (app_display_state_is_on(s_disp_ctx)) {
        enabled = true;
    }

    app_display_unlock();
    return enabled;
}


int app_display_board_config_set(display_board_config_t *config)
{
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");

    if (s_display_board_config == NULL) {
        s_display_board_config = os_malloc(sizeof(display_board_config_t));
        AVDK_RETURN_ON_FALSE(s_display_board_config, AVDK_ERR_GENERIC, TAG, "display_board_config malloc failed");
    }

    os_memcpy(s_display_board_config, config, sizeof(display_board_config_t));

    return AVDK_ERR_OK;
}


display_board_config_t *app_display_board_config_get(void)
{
    return s_display_board_config;
}
