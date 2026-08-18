#include "bk_private/bk_init.h"
#include <components/system.h>
#include <components/log.h>
#include <os/os.h>
#include <components/shell_task.h>
#include <components/bk_frame_buffer.h>
#include <stdint.h>
#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include "gpio_driver.h"
#include "media_service.h"
#include "app_display.h"
#include "app_camera.h"
#include "devices_mgmt.h"
#include "app_gpu.h"
#include "avdk_monitor.h"
#ifdef CONFIG_INTEGRATION_DOORBELL
#include "doorbell_comm.h"
#include "doorbell_config.h"
#endif
#include <lcd/lcd_mipi_hx8399c_1080x1920.h>
#include <doorbell_comm.h>
#include "doorbell_ipc_msg.h"
#include "doorbell_keepalive.h"
#include "doorbell_selftest.h"
#include "ui/doorbell_ui.h"
#include "doorbell_netcfg.h"
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "key_map.h"

/* Firmware/protocol version advertised in the BLE provisioning Manufacturer
 * Specific Data (BCD-style: 0x0100 = v1.0). Bump on protocol changes. */
#define VIDEO_INTERCOM_FW_MAJOR 1
#define VIDEO_INTERCOM_FW_MINOR 0
#define VIDEO_INTERCOM_FW_PATCH 0
#if CONFIG_BOOT_VIDEO_PLAYER
#include "boot_video_player.h"
#endif
#if CONFIG_BOOT_IMAGE_PLAYER
#include "boot_image_player.h"
#endif

#if CONFIG_BOOT_IMAGE_PLAYER || CONFIG_BOOT_VIDEO_PLAYER
/* Boot-media LCD bring-up adapter (dependency inversion): the boot image/video
 * components stay decoupled from multimedia_device_service and reach the panel
 * only through these callbacks, which forward to the single display owner
 * (app_display) the business UI also uses. Shared by both players since they are
 * mutually exclusive at boot. */
static bk_err_t boot_media_lcd_open(void *user, bk_display_ctlr_handle_t *out_handle)
{
    (void)user;
    if (app_mipi_lcd_turn_on(app_display_board_config_get()) != BK_OK)
    {
        return BK_FAIL;
    }
    bk_display_ctlr_handle_t handle = (bk_display_ctlr_handle_t)app_mipi_lcd_handle_get();
    if (handle == NULL)
    {
        return BK_FAIL;
    }
    *out_handle = handle;
    return BK_OK;
}

static bk_err_t boot_media_lcd_close(void *user)
{
    (void)user;
    return app_mipi_lcd_turn_off();
}
#endif

#if CONFIG_BOOT_IMAGE_PLAYER
static const boot_image_display_ops_t s_boot_img_ops = {
    .lcd_open  = boot_media_lcd_open,
    .lcd_close = boot_media_lcd_close,
    .user      = NULL,
};
#endif

#if CONFIG_BOOT_VIDEO_PLAYER
static const boot_video_display_ops_t s_boot_video_ops = {
    .lcd_open  = boot_media_lcd_open,
    .lcd_close = boot_media_lcd_close,
    .user      = NULL,
};
#endif

/* Boot video/image completion -> the panel stays lit (KEEP_ON). Hand it straight
 * to LVGL: the UI controller starts LVGL and selects the first screen (home when
 * already provisioned, otherwise the provisioning/QR page). No screen-off in
 * between so the switch to the UI is seamless. */
static void video_intercom_boot_media_done(bk_err_t result, void *user_data)
{
    doorbell_ui_on_boot_media_done(result, user_data);
}

int main(void)
{
    bk_init();
    media_service_init();

    camera_board_config_t camera_board = {0};
    display_board_config_t display_board = {0};
    gpu_board_config_t gpu_board = {0};

    camera_board.mipi.enable = true;
    camera_board.mipi.pin_scl = GPIO_69;
    camera_board.mipi.pin_sda = GPIO_70;
    camera_board.mipi.i2c_id = 1;
    camera_board.mipi.pin_reset = GPIO_71;
    camera_board.mipi.pin_pwdn = -1;
    camera_board.mipi.pin_xclk = GPIO_59;
    camera_board.mipi.sensor_max_width = 1920;
    camera_board.mipi.sensor_max_height = 1080;
    /* gc2053 1920x1080@15 is a supported sensor mode; 15fps lowers pixel
     * throughput so the concurrent uplink-encode + downlink-decode + GPU
     * composite (+ PIP) pipeline has more per-frame budget for validation. */
    camera_board.mipi.sensor_fps = 15;
    camera_board.mipi.hmirror = 0;
    camera_board.mipi.vflip = 0;
    camera_board.isp.mp_enable = true;
    camera_board.isp.mp_flexa = true;
    /* Uplink encode source = ISP MP. 1280x720 (16:9, 1.5x downscale of 1080p
     * sensor) for phone-side HD preview. Both dimensions MUST be multiples of
     * 16 (720/16=45 FLEXA rows). Downlink decode uses the same 720p class size
     * but is a separate h264d path (shallow DL_SEG_NUM ring); SP stays low-res
     * for the local PIP self-view only. */
    camera_board.isp.mp_width = 1280;
    camera_board.isp.mp_height = 720;
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    /* SP channel feeds the downlink PIP self-view (local camera small window).
     * Reduced to 320x180 (was 640x360): with the concurrent dual-720p pipeline
     * the 640x360@15fps self-view overloaded the GPU compositor / PSRAM bandwidth
     * and caused downlink frame drops. 320x180 is 1/4 the pixels -> ~4x lighter
     * SP write + PIP blit, self-view frame rate unchanged. SP is a non-flexa
     * channel (sp_flexa=false) so it is NOT bound by the 16-line flexa/H.264
     * macroblock rule that MP has: only the WIDTH must be 16-aligned for GPU/DMA
     * stride (320/16=20) and both dims must be even for NV12 4:2:0 (180 is even).
     * MUST stay equal to DL_PIP_WIDTH/HEIGHT in doorbell_downlink_video.c. */
    camera_board.isp.sp_enable = true;
    camera_board.isp.sp_flexa = false;
    camera_board.isp.sp_width = 320;
    camera_board.isp.sp_height = 180;
    camera_board.isp.sp_format = BK_PIXEL_FORMAT_NV12;
    display_board.mipi.enable = true;
    display_board.mipi.pin_reset = GPIO_60;
    display_board.mipi.pin_backlight = GPIO_7;
    display_board.mipi.panel = &lcd_device_hx8399c_mipi_1080x1920;
    display_board.dpu_video.enable = true;
    display_board.dpu_video.decompress = true;
    display_board.dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;


    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 90;
    /* Preview src follows ISP MP (1280x720); dst stays panel pre-rotation
     * 1920x1080, so the preview GPU scales 720p -> 1080P before the 90deg
     * rotation. Must match camera_board.isp.mp_width/height (both /16). */
    gpu_board.flexa.src_width = 1280;
    gpu_board.flexa.src_height = 720;
    gpu_board.flexa.dst_width = 1920;
    gpu_board.flexa.dst_height = 1080;
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = true;
    gpu_board.flexa.tess_width = 0;
    gpu_board.flexa.tess_height = 0;


    bk_frame_buffer_init();

    /* Board config for Multimedia config */
    app_camera_board_config_set(&camera_board);
    app_display_board_config_set(&display_board);
    app_gpu_board_config_set(&gpu_board);

    /* Boot display: async, non-blocking. The component owns the LCD during
     * display and turns it off when done. file_path is provided by the upper
     * layer (SD card /sd0 or internal flash /if0). The boot image and the boot
     * animation are mutually exclusive; prefer the image when both are enabled. */
#if CONFIG_BOOT_IMAGE_PLAYER
    {
        boot_image_play_cfg_t boot_img_cfg = {0};
        boot_img_cfg.file_path           = "/sd0/boot.jpg";
        boot_img_cfg.format              = BOOT_IMAGE_FORMAT_AUTO;
        boot_img_cfg.display_duration_ms = 5000; /* hold 5s, then hand panel to LVGL */
        /* Keep the panel lit after the boot image so LVGL can take over without a
         * visible screen-off flicker (see doorbell_ui_on_boot_media_done). */
        boot_img_cfg.display_mode        = BOOT_IMAGE_DISPLAY_KEEP_ON;
        boot_img_cfg.display_ops         = &s_boot_img_ops;
        /* Boot image must be authored at panel native size (HX8399C 1080x1920). */
        boot_img_cfg.panel_width         = 1080;
        boot_img_cfg.panel_height        = 1920;
        boot_img_cfg.done_cb             = video_intercom_boot_media_done;
        if (boot_image_show(&boot_img_cfg) != BK_OK)
        {
            video_intercom_boot_media_done(BK_FAIL, NULL);
        }
    }
#elif CONFIG_BOOT_VIDEO_PLAYER
    {
        boot_video_play_cfg_t boot_cfg = {0};
        boot_cfg.file_path     = "/sd0/boot.mp4";
        boot_cfg.rotate_degree = BOOT_VIDEO_ROTATE_AUTO;
        boot_cfg.volume        = 80;
        /* Keep the panel lit after the boot animation so LVGL can take over
         * seamlessly (see doorbell_ui_on_boot_media_done). */
        boot_cfg.display_mode  = BOOT_VIDEO_DISPLAY_KEEP_ON;
        boot_cfg.display_ops   = &s_boot_video_ops;
        /* Panel native geometry (HX8399C MIPI 1080x1920), used for AUTO rotation. */
        boot_cfg.panel_width   = 1080;
        boot_cfg.panel_height  = 1920;
        boot_cfg.done_cb       = video_intercom_boot_media_done;
        if (boot_video_play(&boot_cfg) != BK_OK)
        {
            /* Nothing will play -> panel is free now; unblock the QR UI. */
            video_intercom_boot_media_done(BK_FAIL, NULL);
        }
    }
#else
    /* No boot animation configured -> the panel is free right away. */
    video_intercom_boot_media_done(BK_OK, NULL);
#endif

    /* Debug config for Multimedia */
    avdk_monitor_init();
    avdk_monitor_start();

    devices_mgmt_init();

    /* Init the local UI controller before provisioning kicks off in
     * doorbell_core_init(), so it registers the provisioning status callback and
     * catches the very first RUNNING/RECONNECT_* status. LVGL itself is not
     * started here; it comes up once boot media finishes (see
     * video_intercom_boot_media_done -> doorbell_ui_on_boot_media_done). */
    doorbell_ui_init();

    /* Provide the firmware version carried in the BLE provisioning advertisement
     * BEFORE provisioning starts in doorbell_core_init(). The device type
     * (INTERCOM) and the "BK_INTERCOM_<MAC3>" Local Name are handled inside the
     * doorbell_netcfg component per the BLE provisioning adv spec. */
    doorbell_netcfg_set_adv_fw_version(VIDEO_INTERCOM_FW_MAJOR,
                                       VIDEO_INTERCOM_FW_MINOR,
                                       VIDEO_INTERCOM_FW_PATCH);

#if (defined(CONFIG_INTEGRATION_DOORBELL))
    bk_doorbell_config_init();
    /* doorbell_core_init() calls doorbell_boarding_init(), which for this project
     * (CONFIG_DOORBELL_NETCFG) is provided by the doorbell_netcfg component and
     * brings up SDK bk_network_provisioning (BLE provisioning + default
     * reconnect). No separate provisioning init call is needed here. */
    doorbell_core_init();
#endif

#if CONFIG_VOICE_SERVICE_TEST
    int cli_voice_init(void);
    cli_voice_init();
#endif

    doorbell_ipc_wakeup_env_init();
    doorbell_keepalive_handle_wakeup_reason();
    //doorbell_keepalive_cli_init();
    doorbell_selftest_cli_init();

    /* Physical keys (K3-K6 ADC ladder). Started last, after core services
     * are up, so key handlers can safely call into them. No-op unless
     * CONFIG_BK_KEY is enabled. */
#if CONFIG_BK_KEY
    doorbell_key_start();
#endif
    return 0;
}
