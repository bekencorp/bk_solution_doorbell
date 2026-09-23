#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <common/bk_include.h>
#include <components/log.h>
#include <common/bk_err.h>
#include <components/bk_draw_osd.h>
#include <modules/wifi.h>
#include <sys/time.h>
#include <time.h>
#include <stddef.h>
#if CONFIG_AON_RTC
#include <driver/aon_rtc.h>
#endif

#include "doorbell_display_compositor.h"
#include "lcd_osd.h"
#include "lcd_osd_assets.h"
#if CONFIG_CLI
#include "cli.h"
#endif

#define TAG "lcd-osd"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define LCD_OSD_TICK_MS  1000U

/* Same content keys as draw_osd_example blend_dsc.c wifi_group. */
static const char *const s_wifi_content[LCD_OSD_WIFI_LEVELS] = {
    "wifi_rssi_none", "wifi_rssi_1", "wifi_rssi_2", "wifi_rssi_3", "wifi_rssi_full",
};

typedef struct {
    bk_draw_osd_ctlr_handle_t osd;
    beken_mutex_t lock;
    beken_timer_t tick;
    uint8_t inited;
    uint8_t enable;
    uint8_t last_wifi;
    char last_time[9];
} lcd_osd_ctx_t;

static lcd_osd_ctx_t s_ctx = {
    .enable = 0,
    .last_wifi = 0xFFU,
};

static uint8_t lcd_osd_wifi_level(void)
{
    wifi_link_status_t st;
    int rssi;

    os_memset(&st, 0, sizeof(st));
    if (bk_wifi_sta_get_link_status(&st) != BK_OK)
    {
        return 0U;
    }
    if (st.state != WIFI_LINKSTATE_STA_CONNECTED &&
        st.state != WIFI_LINKSTATE_STA_GOT_IP)
    {
        return 0U;
    }
    rssi = st.rssi;
    if (rssi >= -55)
    {
        return 4U;
    }
    if (rssi >= -65)
    {
        return 3U;
    }
    if (rssi >= -75)
    {
        return 2U;
    }
    return 1U;
}

static void lcd_osd_format_time(char *out, size_t out_len)
{
    struct timeval tv;
    struct tm *tmp;
    int got;

    os_memset(&tv, 0, sizeof(tv));
#if CONFIG_AON_RTC
    got = (bk_rtc_gettimeofday(&tv, NULL) == BK_OK) ? 0 : -1;
#else
    got = gettimeofday(&tv, NULL);
#endif
    if (got != 0)
    {
        tv.tv_sec = time(NULL);
    }

    tmp = localtime(&tv.tv_sec);
    if (tmp == NULL)
    {
        os_snprintf(out, (UINT32)out_len, "00:00:00");
        return;
    }
    os_snprintf(out, (UINT32)out_len, "%02d:%02d:%02d",
                tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
}

static void lcd_osd_paint_locked(void)
{
    char now[9];
    uint8_t wifi;
    uint8_t dirty = 0U;

    if (s_ctx.osd == NULL)
    {
        return;
    }

    os_memset(now, 0, sizeof(now));
    lcd_osd_format_time(now, sizeof(now));
    wifi = lcd_osd_wifi_level();

    if (os_strcmp(s_ctx.last_time, now) != 0)
    {
        avdk_err_t upd = bk_draw_osd_add_or_update(s_ctx.osd, "time", now);
        if (upd != AVDK_ERR_OK)
        {
            LOGE("time update failed=%d content=%s\n", (int)upd, now);
        }
        else if (s_ctx.last_time[0] == '\0')
        {
            LOGI("time \"%s\"\n", now);
        }
        os_strncpy(s_ctx.last_time, now, sizeof(s_ctx.last_time) - 1U);
        dirty = 1U;
    }
    if (s_ctx.last_wifi != wifi)
    {
        (void)bk_draw_osd_add_or_update(s_ctx.osd, "wifi_group", s_wifi_content[wifi]);
        s_ctx.last_wifi = wifi;
        dirty = 1U;
    }
    if (dirty != 0U)
    {
        (void)bk_draw_osd_array(s_ctx.osd, NULL);
    }
}

static void lcd_osd_attach(bk_gpu_ctlr_handle_t gpu)
{
    osd_ctlr_config_t cfg;
    avdk_err_t ret;

    if (s_ctx.enable == 0U || gpu == NULL || s_ctx.osd != NULL)
    {
        return;
    }

    os_memset(&cfg, 0, sizeof(cfg));
    cfg.gpu = gpu;
    cfg.panel_w = LCD_OSD_PANEL_W;
    cfg.panel_h = LCD_OSD_PANEL_H;
    cfg.osd_rotate_degree = 0U;
    cfg.src_format = BK_PIXEL_FORMAT_ARGB8888;
    cfg.blend_assets = lcd_osd_assets;
    cfg.blend_info = lcd_osd_info;

    ret = bk_draw_osd_new(&s_ctx.osd, &cfg);
    if (ret != AVDK_ERR_OK)
    {
        s_ctx.osd = NULL;
        LOGE("bk_draw_osd_new failed=%d\n", (int)ret);
        return;
    }

    s_ctx.last_time[0] = '\0';
    s_ctx.last_wifi = 0xFFU;
    (void)bk_draw_osd_array(s_ctx.osd, NULL);
    lcd_osd_paint_locked();
    LOGI("LCD OSD attached (time + wifi)\n");
}

static void lcd_osd_detach(void)
{
    if (s_ctx.osd != NULL)
    {
        (void)bk_draw_osd_delete(s_ctx.osd);
        s_ctx.osd = NULL;
        LOGI("LCD OSD detached\n");
    }
    s_ctx.last_time[0] = '\0';
    s_ctx.last_wifi = 0xFFU;
}

static void lcd_osd_on_gpu_event(bk_gpu_ctlr_handle_t gpu, bool attached)
{
    if (s_ctx.lock == NULL)
    {
        return;
    }
    rtos_lock_mutex(&s_ctx.lock);
    if (attached)
    {
        lcd_osd_detach();
        if (s_ctx.enable != 0U)
        {
            lcd_osd_attach(gpu);
        }
    }
    else
    {
        lcd_osd_detach();
    }
    rtos_unlock_mutex(&s_ctx.lock);
}

static void lcd_osd_tick(void *arg)
{
    (void)arg;
    if (s_ctx.lock == NULL)
    {
        return;
    }
    rtos_lock_mutex(&s_ctx.lock);
    lcd_osd_paint_locked();
    rtos_unlock_mutex(&s_ctx.lock);
}

#if CONFIG_CLI
static void lcd_osd_apply_enable_locked(uint8_t enable)
{
    if (s_ctx.enable == enable)
    {
        return;
    }
    s_ctx.enable = enable;
    if (enable != 0U)
    {
        if (doorbell_compositor_is_running())
        {
            lcd_osd_attach(doorbell_compositor_gpu_handle_get());
        }
    }
    else
    {
        lcd_osd_detach();
    }
}

static void lcd_osd_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (s_ctx.lock == NULL)
    {
        LOGE("OSD not inited\n");
        return;
    }

    if (argc >= 2 && argv[1] != NULL)
    {
        if (os_strcmp(argv[1], "on") == 0)
        {
            rtos_lock_mutex(&s_ctx.lock);
            lcd_osd_apply_enable_locked(1U);
            rtos_unlock_mutex(&s_ctx.lock);
            LOGI("osd on%s\n", (s_ctx.osd != NULL) ? " (attached)" : " (idle, waits for videoIntercom)");
            return;
        }
        if (os_strcmp(argv[1], "off") == 0)
        {
            rtos_lock_mutex(&s_ctx.lock);
            lcd_osd_apply_enable_locked(0U);
            rtos_unlock_mutex(&s_ctx.lock);
            LOGI("osd off\n");
            return;
        }
        LOGW("usage: osd [on|off]\n");
        return;
    }

    LOGI("osd %s, overlay %s\n",
         (s_ctx.enable != 0U) ? "on" : "off",
         (s_ctx.osd != NULL) ? "attached" : "idle");
}

static const struct cli_command s_lcd_osd_cli[] = {
    {"osd", "osd [on|off]", lcd_osd_cli_cmd},
};
#endif

bk_err_t lcd_osd_init(void)
{
    bk_err_t ret;

    if (s_ctx.inited != 0U)
    {
        return BK_OK;
    }

    ret = rtos_init_mutex(&s_ctx.lock);
    if (ret != BK_OK)
    {
        LOGE("mutex init failed\n");
        return BK_FAIL;
    }

    doorbell_compositor_set_gpu_event_cb(lcd_osd_on_gpu_event);

#if CONFIG_CLI
    (void)cli_register_commands(s_lcd_osd_cli,
                                (int)(sizeof(s_lcd_osd_cli) / sizeof(s_lcd_osd_cli[0])));
#endif

    ret = rtos_init_timer(&s_ctx.tick, LCD_OSD_TICK_MS, lcd_osd_tick, NULL);
    if (ret != BK_OK)
    {
        LOGE("timer init failed\n");
        doorbell_compositor_set_gpu_event_cb(NULL);
        return BK_FAIL;
    }
    ret = rtos_start_timer(&s_ctx.tick);
    if (ret != BK_OK)
    {
        LOGE("timer start failed\n");
        doorbell_compositor_set_gpu_event_cb(NULL);
        return BK_FAIL;
    }

    s_ctx.inited = 1U;
    LOGI("LCD OSD ready (osd on|off; binds on compositor gpu open)\n");
    return BK_OK;
}
