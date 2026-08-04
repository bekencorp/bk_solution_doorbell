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
#include "app_camera.h"
#include "devices_mgmt.h"
#include "avdk_monitor.h"
#ifdef CONFIG_INTEGRATION_DOORBELL
#include "doorbell_comm.h"
#include "bk_smart_config.h"
#endif
#include "doorbell_ipc_msg.h"
#include "doorbell_keepalive.h"

int main(void)
{
    bk_init();
    media_service_init();

    BK_LOGI(NULL, "AP main running...\r\n");

    camera_board_config_t camera_board = {0};

    camera_board.mipi.enable = true;
    camera_board.mipi.pin_scl = GPIO_69;
    camera_board.mipi.pin_sda = GPIO_70;
    camera_board.mipi.i2c_id = 1;
    camera_board.mipi.pin_reset = GPIO_71;
    camera_board.mipi.pin_pwdn = -1;
    camera_board.mipi.pin_xclk = GPIO_59;
    camera_board.mipi.sensor_max_width = 2304;
    camera_board.mipi.sensor_max_height = 1296;
    camera_board.mipi.sensor_fps = 20;
    camera_board.isp.mp_enable = true;
    camera_board.isp.mp_flexa = true;
    camera_board.isp.mp_width = 2304;
    camera_board.isp.mp_height = 1296;
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    camera_board.isp.sp_enable = false;
    camera_board.isp.sp_flexa = false;

    bk_frame_buffer_init();

    /* Board config for Multimedia config */
    app_camera_board_config_set(&camera_board);

    /* Debug config for Multimedia */
    avdk_monitor_init();
    avdk_monitor_start();

    devices_mgmt_init();

#if (defined(CONFIG_INTEGRATION_DOORBELL))
    bk_smart_config_init();
    doorbell_core_init();

#if (CONFIG_ASR_SERVICE_WITH_MIC)
    extern int doorbell_asr_turn_on(void);
    doorbell_asr_turn_on();
#endif

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
