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
#include "bk_smart_config.h"
#endif
#include <lcd/lcd_mipi_hx8399c_1080x1920.h>
#include <doorbell_comm.h>
#include "doorbell_ipc_msg.h"
#include "doorbell_keepalive.h"

int main(void)
{
    bk_init();
    media_service_init();

    BK_LOGI(NULL, "AP main running...\r\n");

    camera_board_config_t camera_board = {0};
    display_board_config_t display_board = {0};
    gpu_board_config_t gpu_board = {0};

    camera_board.mipi.enable = true;
    /* BK7259QN128B832 (B package): pins P69/P70/P71 (GPIO69/70/71) are no
     * longer bonded out (those pads became audio/power pins). Camera I2C1 and
     * reset are remapped to GPIOs that exist in the B package. GPIO_64/GPIO_65
     * are the free digital pads adjacent to the camera connector cluster
     * (B pins 118/119); reset moves to GPIO_55 (nearest free digital pad).
     * Adjust these to match the actual B-package board schematic if different. */
    camera_board.mipi.pin_scl = GPIO_64;
    camera_board.mipi.pin_sda = GPIO_65;
    camera_board.mipi.i2c_id = 1;
    camera_board.mipi.pin_reset = GPIO_55;
    camera_board.mipi.pin_pwdn = -1;
    camera_board.mipi.pin_xclk = GPIO_59;
    camera_board.mipi.sensor_max_width = 1920;
    camera_board.mipi.sensor_max_height = 1080;
    camera_board.mipi.sensor_fps = 25;
    camera_board.mipi.hmirror = 0;
    camera_board.mipi.vflip = 0;
    camera_board.isp.mp_enable = true;
    camera_board.isp.mp_flexa = true;
    camera_board.isp.mp_width = 1920;
    camera_board.isp.mp_height = 1080;
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    camera_board.isp.sp_enable = false;
    camera_board.isp.sp_flexa = false;
    display_board.mipi.enable = true;
    display_board.mipi.pin_reset = GPIO_60;
    display_board.mipi.pin_backlight = GPIO_7;
    display_board.mipi.panel = &lcd_device_hx8399c_mipi_1080x1920;
    display_board.dpu_video.enable = true;
    display_board.dpu_video.decompress = true;
    display_board.dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;


    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 90;
    gpu_board.flexa.src_width = 1920;
    gpu_board.flexa.src_height = 1080;
    gpu_board.flexa.dst_width = 1920;
    gpu_board.flexa.dst_height = 1080;
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = false;
    gpu_board.flexa.tess_width = 0;
    gpu_board.flexa.tess_height = 0;


    bk_frame_buffer_init();

    /* Board config for Multimedia config */
    app_camera_board_config_set(&camera_board);
    app_display_board_config_set(&display_board);
    app_gpu_board_config_set(&gpu_board);

    /* Debug config for Multimedia */
    avdk_monitor_init();
    avdk_monitor_start();

    devices_mgmt_init();

#if (defined(CONFIG_INTEGRATION_DOORBELL))
    bk_smart_config_init();
    doorbell_core_init();
#endif

#if CONFIG_VOICE_SERVICE_TEST
    int cli_voice_init(void);
    cli_voice_init();
#endif

    doorbell_ipc_wakeup_env_init();
    doorbell_keepalive_handle_wakeup_reason();
    //doorbell_keepalive_cli_init();
    return 0;
}
