#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <os/str.h>
#include <components/shell_task.h>
#include "cli.h"

#include "devices_mgmt.h"
#include <components/bk_frame_buffer.h>

#include <avdk_utils.h>

#include "app_camera.h"
#include "app_display.h"
#include "app_gpu.h"
#include "app_codec.h"
#include "bk_image_action.h"
#include <components/bk_lcd_panel.h>
#include <components/bk_camera_bus.h>
#include <components/bk_flexa_bond.h>
#include "app_jpeg_decode.h"
#include <components/bk_encode/bk_h264_encode_ctlr.h>
#ifdef CONFIG_MDS_SNAPSHOT
#include "bk_snapshot_sw.h"
#endif
#include <lcd/lcd_mipi_hx8399c_1080x1920.h>
#include <lcd/lcd_mipi_hx8394f_720x1280.h>

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define TAG "db-cli"

#if CONFIG_VOICE_SERVICE
void cli_doorbell_audio_turn_on(uint32_t aec, uint32_t uac, uint32_t sample_rate, uint32_t fmt);
void cli_doorbell_audio_turn_off(void);
#endif
#if (CONFIG_ASR_SERVICE)
void cli_doorbell_asr_turn_on(uint32_t aec, uint32_t uac, uint32_t sample_rate, uint8_t asr_en);
void cli_doorbell_asr_turn_off(void);
#endif

#define CMD_CONTAIN(value) cmd_contain(argc, argv, value)
#define GET_PPI(value)     get_ppi_from_cmd(argc, argv, value)
#define GET_NAME(value)    get_name_from_cmd(argc, argv, value)
#define GET_ROTATE()    get_rotate_from_cmd(argc, argv)


//isp open [mipi|dvp|dual] [camera_width] [camera_height] [isp_output_width] [isp_output_height]
//isp open mipi 1280 720 960 412
//isp open mipi 1920 1080 960 540
void cli_avdk_mds_isp_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;

    LOGI("%s, %d, argc=%d\n", __func__, __LINE__, argc);
    
    // Parse command from argv[1]: "isp_dump", "open", or "close"
    if (argc < 2 || argv[1] == NULL)
    {
        LOGE("Usage: isp [isp_dump|open|close] [mipi|dvp|dual] [width] [height] [isp_output_width] [isp_output_height]\n");
        return;
    }

    void isp_reg_dump_new();
    if (os_strcmp(argv[1], "isp_dump") == 0)
    {
        isp_reg_dump_new();
        return;
    }

    if (os_strcmp(argv[1], "sns_read") == 0)
    {
        if (argc < 4 || argv[2] == NULL || argv[3] == NULL)
        {
            LOGE("Usage: isp sns_read <addr> <width:8|16>\n");
            return;
        }

        unsigned long reg_addr = os_strtoul(argv[2], NULL, 0);
        unsigned long width = os_strtoul(argv[3], NULL, 10);
        bk_camera_bus_t *bus = bk_camera_bus_get();
        if (bus == NULL)
        {
            LOGE("Camera bus not initialized\n");
            return;
        }

        if ((width != 8) && (width != 16))
        {
            LOGE("Invalid width %lu, only 8 or 16 are supported\n", width);
            return;
        }

        uint32_t value = 0;
        if (width == 8)
        {
            if (bus->read8(bus, (uint32_t)reg_addr, (uint8_t *)&value) != BK_OK)
            {
                LOGE("I2C read8 failed at addr=0x%lx\n", reg_addr);
                return;
            }
            LOGI("Sensor reg[0x%lx] (8-bit) = 0x%02x\n", reg_addr, (unsigned int)(value & 0xFF));
        }
        else
        {
            if (bus->read16(bus, (uint32_t)reg_addr, (uint8_t *)&value) != BK_OK)
            {
                LOGE("I2C read16 failed at addr=0x%lx\n", reg_addr);
                return;
            }
            LOGI("Sensor reg[0x%lx] (16-bit) = 0x%04x\n", reg_addr, (unsigned int)(value & 0xFFFF));
        }

        return;
    }

    if (os_strcmp(argv[1], "sns_write") == 0)
    {
        if (argc < 5 || argv[2] == NULL || argv[3] == NULL || argv[4] == NULL)
        {
            LOGE("Usage: isp sns_write <addr> <value> <width:8|16>\n");
            return;
        }

        unsigned long reg_addr = os_strtoul(argv[2], NULL, 0);
        unsigned long reg_val  = os_strtoul(argv[3], NULL, 0);
        unsigned long width    = os_strtoul(argv[4], NULL, 10);

        bk_camera_bus_t *bus = bk_camera_bus_get();
        if (bus == NULL)
        {
            LOGE("Camera bus not initialized\n");
            return;
        }

        if ((width != 8) && (width != 16))
        {
            LOGE("Invalid width %lu, only 8 or 16 are supported\n", width);
            return;
        }

        if (width == 8)
        {
            uint8_t v8 = (uint8_t)(reg_val & 0xFF);
            if (bus->write8(bus, (uint32_t)reg_addr, (uint32_t)v8) != BK_OK)
            {
                LOGE("I2C write8 failed at addr=0x%lx, val=0x%02x\n", reg_addr, (unsigned int)v8);
                return;
            }
            LOGI("Sensor reg[0x%lx] (8-bit) <= 0x%02x\n", reg_addr, (unsigned int)v8);
        }
        else
        {
            uint16_t v16 = (uint16_t)(reg_val & 0xFFFF);
            if (bus->write16(bus, (uint32_t)reg_addr, (uint32_t)v16) != BK_OK)
            {
                LOGE("I2C write16 failed at addr=0x%lx, val=0x%04x\n", reg_addr, (unsigned int)v16);
                return;
            }
            LOGI("Sensor reg[0x%lx] (16-bit) <= 0x%04x\n", reg_addr, (unsigned int)v16);
        }

        return;
    }

    if (os_strcmp(argv[1], "open") == 0)
    {
        camera_parameters_ext_t *paramters = os_malloc(sizeof(camera_parameters_ext_t));
        os_memset(paramters, 0, sizeof(camera_parameters_ext_t));

        paramters->fps = 20;

        // Parse parameters: open [mipi|dvp|dual] [width] [height] [isp_output_width] [isp_output_height]
        // argv[0]=command, argv[1]="open", argv[2]=interface type, parameters start from argv[3]
        paramters->camera_width = (argc > 3) ? (uint16_t)os_strtoul(argv[3], NULL, 10) : 1280;
        paramters->camera_height = (argc > 4) ? (uint16_t)os_strtoul(argv[4], NULL, 10) : 720;
        paramters->isp_output_width = (argc > 5) ? (uint16_t)os_strtoul(argv[5], NULL, 10) : (app_camera_board_config_get() ? app_camera_board_config_get()->isp.mp_width : 0);
        paramters->isp_output_height = (argc > 6) ? (uint16_t)os_strtoul(argv[6], NULL, 10) : (app_camera_board_config_get() ? app_camera_board_config_get()->isp.mp_height : 0);

        LOGI("Camera: input=%dx%d, ISP output=%dx%d, fps=%d\n",
             paramters->camera_width, paramters->camera_height,
             paramters->isp_output_width, paramters->isp_output_height, paramters->fps);


        camera_board_config_t camera_config = {0};
        if (app_camera_board_config_get() != NULL) {
            os_memcpy(&camera_config, app_camera_board_config_get(), sizeof(camera_board_config_t));
        }
        camera_config.mipi.enable = true;
        camera_config.mipi.sensor_max_width = paramters->camera_width;
        camera_config.mipi.sensor_max_height = paramters->camera_height;
        camera_config.mipi.sensor_fps = paramters->fps;
        camera_config.isp.mp_enable = true;
        camera_config.isp.mp_width = paramters->isp_output_width;
        camera_config.isp.mp_height = paramters->isp_output_height;
        camera_config.isp.mp_format = BK_PIXEL_FORMAT_NV12;
        camera_config.isp.sp_enable = false;
        camera_config.isp.mp_flexa = true;

        app_camera_board_config_set(&camera_config);


        // Parse interface type from argv[2]
        if (argc > 2 && argv[2] != NULL)
        {
            if (os_strcmp(argv[2], "mipi") == 0)
            {
                ret = app_isp_mipi_camera_turn_on(app_camera_board_config_get());
            }
            else if (os_strcmp(argv[2], "dvp") == 0)
            {
                ret = app_isp_dvp_camera_turn_on(paramters);
            }
            else if (os_strcmp(argv[2], "dual") == 0)
            {
                ret = app_isp_dual_camera_turn_on(paramters);
            }
            else if (os_strcmp(argv[2], "change") == 0)
            {
                ret = app_isp_dual_camera_port_change();
            }
            else if (os_strcmp(argv[2], "dvp_on") == 0)
            {
                ret = app_isp_dual_dvp_on(paramters);
            }
            else
            {
                LOGE("Unknown interface type: %s\n", argv[2]);
                os_free(paramters);
                return;
            }
        }
        else
        {
            LOGE("Missing interface type (mipi/dvp/dual)\n");
            os_free(paramters);
            return;
        }

        if (ret != AVDK_ERR_OK)
        {
            LOGI("%s, %d turn on failed, ret=%d\n", __func__, __LINE__, ret);
        }
        else
        {
            LOGI("%s, %d turn on success\n", __func__, __LINE__);
        }
    }
    else if (os_strcmp(argv[1], "close") == 0)
    {
        ret = app_isp_camera_turn_off();
        if (ret != AVDK_ERR_OK)
        {
            LOGI("%s, %d turn off failed, ret=%d\n", __func__, __LINE__, ret);
        }
        else
        {
            LOGI("%s, %d turn off success\n", __func__, __LINE__);
        }
    }
    else if (os_strcmp(argv[1], "soft_reset") == 0)
    {
        ret = app_isp_camera_soft_reset();
        if (ret != AVDK_ERR_OK)
        {
            LOGI("%s, %d soft reset failed, ret=%d\n", __func__, __LINE__, ret);
        }
    }
    else if (os_strcmp(argv[1], "tuning") == 0)
    {
        if (argc < 2 || argv[2] == NULL)
        {
            LOGE("Usage: isp tuning start|stop\n");
            return;
        }

        // External function declarations for ISP tuning server
        void isp_tuning_server_init(void);
        void isp_tuning_server_deinit(void);

        if (os_strcmp(argv[2], "start") == 0)
        {
            // Start ISP tuning server
            LOGI("Starting ISP tuning server...\n");
            isp_tuning_server_init();
            LOGI("ISP tuning server started successfully\n");
            ret = AVDK_ERR_OK;
        }
        else if (os_strcmp(argv[2], "stop") == 0)
        {
            // Stop ISP tuning server
            LOGI("Stopping ISP tuning server...\n");
            isp_tuning_server_deinit();
            LOGI("ISP tuning server stopped successfully\n");
            ret = AVDK_ERR_OK;
        }
        else
        {
            LOGE("Invalid command: %s (expected start or stop)\n", argv[2]);
            LOGE("Usage: isp tuning start|stop\n");
            ret = AVDK_ERR_UNSUPPORTED;
        }
    }
    else
    {
        LOGE("Unknown command: %s\n", argv[1]);
    }
}


extern gpu_board_config_t *gpu_board_config;
static void *s_isp_gpu_bond = NULL;
static void *s_isp_h264e_bond = NULL;
static void *s_mjpegd_gpu_joint_bond = NULL;
static void *s_mjpegd_h264e_bond = NULL;
//display open [panel_name] [gpu_in_w] [gpu_in_h] [gpu_out_w] [gpu_out_h] [rotate]
//display open hx8399c_mipi_1080x1920 960 412 412 960 90
//display open (uses default panel from board config if available)
//display list  (lists all available panels)
void cli_avdk_mds_display_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;

    LOGI("%s, %d, argc=%d\n", __func__, __LINE__, argc);

    // Get all MIPI panel devices
    const bk_display_dsi_panel_t *mipi_panels[20];
    uint32_t num_mipi_panels = bk_lcd_get_mipi_panel_list(mipi_panels, 20);
    LOGI("Found %d MIPI panel(s)\n", num_mipi_panels);

    if (CMD_CONTAIN("list"))
    {
        // List all available panels
        LOGI("Available MIPI panels (%d):\n", num_mipi_panels);
        for (uint32_t i = 0; i < num_mipi_panels; i++) {
            if (mipi_panels[i] != NULL && mipi_panels[i]->name != NULL) {
                LOGI("  [%d] %s\n", i, mipi_panels[i]->name);
            }
        }
        return;
    }

    if (CMD_CONTAIN("open"))
    {
        // Parse parameters: open [panel_name] [gpu_in_w] [gpu_in_h] [gpu_out_w] [gpu_out_h] [rotate]
        // argv[0]=command, argv[1]="open", parameters start from argv[2]
        
        const bk_display_dsi_panel_t *selected_panel = NULL;
        const char *panel_name = NULL;
        int param_offset = 2;  // Default: parameters start from argv[2]

        // Check if argv[2] is a panel name (non-numeric) or a number (gpu_in_w)
        // If it's a valid panel name, use it; otherwise treat it as gpu_in_w
        if (argc > 2 && argv[2] != NULL) {
            // Try to find panel by name
            for (uint32_t i = 0; i < num_mipi_panels; i++) {
                if (mipi_panels[i] != NULL && mipi_panels[i]->name != NULL) {
                    if (os_strcmp(argv[2], mipi_panels[i]->name) == 0) {
                        selected_panel = mipi_panels[i];
                        panel_name = argv[2];
                        param_offset = 3;  // Parameters start from argv[3] if panel_name is provided
                        LOGI("Found panel by name: %s\n", panel_name);
                        break;
                    }
                }
            }
        }

        // If no panel found by name, try to use default from board config
        if (selected_panel == NULL) {
            display_board_config_t *cfg = app_display_board_config_get();
            if (cfg && cfg->mipi.panel) {
                selected_panel = cfg->mipi.panel;
                panel_name = selected_panel->name;
                LOGI("Using default panel from board config: %s\n", panel_name);
            }
        }

        // Parse GPU parameters (offset by param_offset)
        uint16_t gpu_in_w = (argc > param_offset) ? (uint16_t)os_strtoul(argv[param_offset], NULL, 10) : (gpu_board_config ? gpu_board_config->flexa.src_width : 0);
        uint16_t gpu_in_h = (argc > param_offset + 1) ? (uint16_t)os_strtoul(argv[param_offset + 1], NULL, 10) : (gpu_board_config ? gpu_board_config->flexa.src_height : 0);
        uint16_t gpu_out_w = (argc > param_offset + 2) ? (uint16_t)os_strtoul(argv[param_offset + 2], NULL, 10) : (gpu_board_config ? gpu_board_config->flexa.dst_width : 0);
        uint16_t gpu_out_h = (argc > param_offset + 3) ? (uint16_t)os_strtoul(argv[param_offset + 3], NULL, 10) : (gpu_board_config ? gpu_board_config->flexa.dst_height : 0);
        uint8_t rotate = (argc > param_offset + 4) ? (uint8_t)os_strtoul(argv[param_offset + 4], NULL, 10) : (gpu_board_config ? gpu_board_config->flexa.degree : 0);

        LOGI("Config: panel=%s, gpu_in=%dx%d, gpu_out=%dx%d, rotate=%d\n",
             panel_name, gpu_in_w, gpu_in_h, gpu_out_w, gpu_out_h, rotate);


        gpu_board_config_t gpu_config = {
            .flexa = {
                .src_width = gpu_in_w,
                .src_height = gpu_in_h,
                .dst_width = gpu_out_w,
                .dst_height = gpu_out_h,
                .degree = rotate,
                .src_format = BK_PIXEL_FORMAT_NV12,
                .dst_format = BK_PIXEL_FORMAT_ARGB8888,
                .dst_compress = true,
                .scale = false,
                .enable = true,
                .tess_width = 0,
                .tess_height = 0,
            },
        };
        app_gpu_board_config_set(&gpu_config);


        if (selected_panel != NULL)
        {
            display_board_config_t display_config = {0};
            if (app_display_board_config_get() != NULL) {
                os_memcpy(&display_config, app_display_board_config_get(), sizeof(display_board_config_t));
            }
            display_config.mipi.enable = true;
            display_config.mipi.panel = selected_panel;
            display_config.dpu_video.enable = true;
            display_config.dpu_video.decompress = true;
            display_config.dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;
            app_display_board_config_set(&display_config);
        }
        
        ret = app_mipi_lcd_turn_on(app_display_board_config_get());
        if (ret != AVDK_ERR_OK)
        {
            // Turn on display using selected panel
            LOGI("Turning on panel: %s (%p) failed, ret=%d\n", selected_panel->name, selected_panel, ret);
        }
        // Turn on GPU with parsed parameters (0 or 0xFF means use default)
        ret = app_gpu_turn_on(app_gpu_board_config_get());
        if (ret != AVDK_ERR_OK)
        {
            LOGI("Turning on GPU failed, ret=%d\n", ret);
        }
        void *isp_handle = app_isp_handle_get();
        if (isp_handle == NULL) {
            LOGE("%s, app_isp_handle_get failed\n", __func__);
            return;
        }
        bk_gpu_ctlr_handle_t gpu_handle = app_gpu_handle_get();
        if (gpu_handle == NULL) {
            LOGE("%s, app_gpu_handle_get failed\n", __func__);
            return;
        }
        ret = bk_flexa_isp_gpu_bond_start(&s_isp_gpu_bond, isp_handle, gpu_handle);
        if (ret != BK_OK) {
            LOGE("%s, bk_flexa_isp_gpu_bond_start failed, ret = %d\n", __func__, ret);
            return;
        }
    }

    if (CMD_CONTAIN("close"))
    {
        ret = app_mipi_lcd_turn_off();
        if (ret == BK_OK) {
            LOGI("Display turned off successfully\n");
        } else {
            LOGE("Failed to turn off display, ret=%d\n", ret);
        }
    }
}

#include "hpdma_hal.h"
#include <driver/hpdma.h>
#include <driver/hal/hal_hpdma_types.h>
#include "hpdma_driver.h"

static void hpdma_link_transfer_complete_callback(hpdma_id_t hpdma_id, void *user_data)
{
    CLI_LOGD("hpdma link transfer callback(%d)\n", hpdma_id);

    // Release semaphore to notify transfer completion
    // user_data points to the semaphore handle (beken_semaphore_t*)
    if (user_data != NULL) {
        beken_semaphore_t *sem_ptr = (beken_semaphore_t *)user_data;
        rtos_set_semaphore(sem_ptr);
        CLI_LOGD("Semaphore released in callback\n");
    }
}

void test_hdma(void)
{
    int width = 16, height = 16, src_x = 4, src_y = 4;
    int dst_x = 8, dst_y = 8, src_width = 8, src_height = 8;
    uint8_t *src_frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, width * height);
    uint8_t *dst_frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, width * height);

    for (int i = 0; i < width * height; i++)
    {
        ((uint8_t *)src_frame)[i] = 0xFF;
    }

    for (int j = 0; j < src_height; j++)
    {
        for (int i = 0; i < src_width; i++)
        {
            ((uint8_t *)src_frame)[(j + src_y) * width + i + src_x] = 0x88;
        }
    }

    avdk_hex_dump(src_frame, width * height, 0);

    for (int i = 0; i < width * height; i++)
    {
        ((uint8_t *)dst_frame)[i] = 0xFF;
    }

    void *desc_table = bk_hpdma_link_init(1);

    hpdma_link_config_t configs[1];
    configs[0].src_addr = (uint32_t)(src_frame + src_x + src_y * width);
    configs[0].dst_addr = (uint32_t)(dst_frame + dst_x + dst_y * width);
    configs[0].src_xsize = src_width;
    configs[0].src_ysize = src_height;
    configs[0].dst_xsize = src_width;
    configs[0].dst_ysize = src_height;
    configs[0].src_step = width - src_width;
    configs[0].dst_step = width - src_width;
    configs[0].finish_int_en = 1;  // Enable interrupt on last descriptor
    configs[0].half_finish_int_en = 0;

    BK_LOG_ON_ERR(bk_hpdma_link_set_descs(desc_table, configs, 1));

    hpdma_id_t dma_id = bk_hpdma_alloc(HPDMA_DEV_DTCM);
    if (dma_id >= HPDMA_ID_MAX) {
        CLI_LOGE("Failed to allocate DMA channel\r\n");
        bk_hpdma_link_deinit(desc_table);
        return;
    }

    CLI_LOGD("Starting 1D linked list transfer with channel %d...\r\n", dma_id);

    // Create semaphore for synchronization
    beken_semaphore_t transfer_sem = NULL;
    bk_err_t sem_ret = rtos_init_semaphore(&transfer_sem, 1);
    if (sem_ret != BK_OK) {
        CLI_LOGE("Failed to create semaphore\r\n");
        bk_hpdma_free(HPDMA_DEV_DTCM, dma_id);
        bk_hpdma_link_deinit(desc_table);
        return;
    }

    // Register ISR with semaphore as user_data
    BK_LOG_ON_ERR(bk_hpdma_register_isr(dma_id, NULL, NULL, hpdma_link_transfer_complete_callback, (void *)&transfer_sem));

    // Start transfer (asynchronous)
    BK_LOG_ON_ERR(bk_hpdma_link_transfer(dma_id, desc_table));
    BK_LOG_ON_ERR(bk_hpdma_enable_finish_interrupt(dma_id));

    CLI_LOGD("Waiting for transfer completion...\r\n");

    // Wait for semaphore (transfer completion signal from callback)
    bk_err_t wait_ret = rtos_get_semaphore(&transfer_sem, BEKEN_WAIT_FOREVER);
    if (wait_ret != BK_OK) {
        CLI_LOGE("Failed to wait for semaphore: %d\r\n", wait_ret);
        rtos_deinit_semaphore(&transfer_sem);
        bk_hpdma_free(HPDMA_DEV_DTCM, dma_id);
        bk_hpdma_link_deinit(desc_table);
        return;
    }

    avdk_hex_dump(dst_frame, width * height, 0);

    bk_frame_buffer_free(src_frame);
    bk_frame_buffer_free(dst_frame);
    rtos_deinit_semaphore(&transfer_sem);
    bk_hpdma_free(HPDMA_DEV_DTCM, dma_id);
    bk_hpdma_link_deinit(desc_table);
}

/* MIPI LCD + DPU：joint_test open mipi / open uvc 时先开屏，再启相机链路 */
static void joint_test_mipi_lcd_on(void)
{
    display_board_config_t display_board = {0};
    if (app_display_board_config_get() != NULL) {
        os_memcpy(&display_board, app_display_board_config_get(), sizeof(display_board_config_t));
    }
    display_board.mipi.enable = true;
    if (display_board.mipi.panel == NULL) {
        display_board.mipi.panel = &lcd_device_hx8399c_mipi_1080x1920;
    }
    display_board.dpu_video.enable = true;
    display_board.dpu_video.decompress = true;
    display_board.dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;
    app_display_board_config_set(&display_board);
    app_mipi_lcd_turn_on(app_display_board_config_get());
}

/// @brief joint_test open mipi [720p|1080p] [fps] [h264e] | open uvc [width height] [h264e]
/// @param pcWriteBuffer 
/// @param xWriteBufferLen 
/// @param argc 
/// @param argv 
/// ap_cmd joint_test open mipi 1080p 25
//  ap_cmd joint_test open mipi 720p 30

void cli_avdk_mds_joint_test_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;

    if (argc >= 2 && argv[1] != NULL && os_strcmp(argv[1], "open") == 0) 
    {
        if (argc >= 3 && argv[2] != NULL && os_strcmp(argv[2], "mipi") == 0) {
            bool want_h264e = false;
            bool use_1080p = false;
            uint16_t sensor_fps = 25;
            camera_board_config_t camera_board = {0};
            if (app_camera_board_config_get() != NULL) {
                os_memcpy(&camera_board, app_camera_board_config_get(), sizeof(camera_board_config_t));
            }
            gpu_board_config_t gpu_board = {0};

            for (int i = 3; i < argc; ++i) {
                if (argv[i] == NULL) {
                    continue;
                }

                if (os_strcmp(argv[i], "h264e") == 0) {
                    want_h264e = true;
                    continue;
                }

                if (os_strcmp(argv[i], "720p") == 0) {
                    use_1080p = false;
                    continue;
                }

                if (os_strcmp(argv[i], "1080p") == 0) {
                    use_1080p = true;
                    continue;
                }

                sensor_fps = (uint16_t)os_strtoul(argv[i], NULL, 10);
                if (sensor_fps == 0) {
                    LOGE("Usage: joint_test open mipi [720p|1080p] [fps] [h264e]\n");
                    return;
                }
            }

            joint_test_mipi_lcd_on();

            camera_board.mipi.enable = true;
            if (use_1080p) {
                camera_board.mipi.sensor_max_width = 1920;
                camera_board.mipi.sensor_max_height = 1080;
                camera_board.isp.mp_width = 1920;
                camera_board.isp.mp_height = 1080;
            } else {
                camera_board.mipi.sensor_max_width = 1280;
                camera_board.mipi.sensor_max_height = 720;
                camera_board.isp.mp_width = 1280;
                camera_board.isp.mp_height = 720;
            }
            camera_board.mipi.sensor_fps = sensor_fps;
            LOGI("joint_test open mipi %s fps=%u h264e=%d\n",
                 use_1080p ? "1080p" : "720p",
                 camera_board.mipi.sensor_fps,
                 want_h264e);
            camera_board.isp.mp_enable = true;
            camera_board.isp.mp_flexa = true;
            camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
            camera_board.isp.sp_enable = false;
            camera_board.isp.sp_flexa = false;
            app_camera_board_config_set(&camera_board);
            ret = app_isp_mipi_camera_turn_on(app_camera_board_config_get());
            if (ret != BK_OK) {
                LOGE("%s, app_isp_mipi_camera_turn_on failed, ret = %d\n", __func__, ret);
                return;
            }

            ret = devices_mgmt_set_display_source(DISPLAY_STREAM_ID_MIPI_CSI, NULL);
            if (ret != BK_OK) {
                LOGE("%s, devices_mgmt_set_display_source(MIPI_CSI) failed, ret = %d\n", __func__, ret);
                return;
            }

            void *isp_handle = app_isp_handle_get();
            if (isp_handle == NULL) {
                LOGE("%s, app_isp_handle_get failed\n", __func__);
                return;
            }

            if (want_h264e) {
                int h264_ret = app_h264e_turn_on();
                if (h264_ret != BK_OK) {
                    LOGE("%s, app_h264e_turn_on failed, ret = %d\n", __func__, h264_ret);
                    return;
                }
                bk_h264_encode_ctlr_handle_t enc_handle =
                    (bk_h264_encode_ctlr_handle_t)app_h264_encode_handle_get();
                if (enc_handle == NULL) {
                    LOGE("%s, app_h264_encode_handle_get failed\n", __func__);
                    return;
                }
                ret = bk_flexa_isp_h264e_bond_start(&s_isp_h264e_bond, isp_handle, enc_handle);
                if (ret != BK_OK) {
                    LOGE("%s, bk_flexa_isp_h264e_bond_start failed, ret = %d\n", __func__, ret);
                    return;
                }
            }
            if (use_1080p) {
                gpu_board.flexa.src_width = 1920;
                gpu_board.flexa.src_height = 1080;
                gpu_board.flexa.dst_width = 1920;
                gpu_board.flexa.dst_height = 1080;
                gpu_board.flexa.scale = false;
            } else {
                gpu_board.flexa.src_width = 1280;
                gpu_board.flexa.src_height = 720;
                gpu_board.flexa.dst_width = 1920;
                gpu_board.flexa.dst_height = 1080;
                gpu_board.flexa.scale = true;
            }
            gpu_board.flexa.degree = 90;
            gpu_board.flexa.enable = true;
            gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
            gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
            gpu_board.flexa.dst_compress = true;
            gpu_board.flexa.tess_width = 0;
            gpu_board.flexa.tess_height = 0;
            app_gpu_board_config_set(&gpu_board);
            ret = app_gpu_turn_on(app_gpu_board_config_get());
            if (ret != AVDK_ERR_OK) {
                LOGE("%s, app_gpu_turn_on failed, ret = %d\n", __func__, ret);
                return;
            }

            bk_gpu_ctlr_handle_t gpu_handle = app_gpu_handle_get();
            if (gpu_handle == NULL) {
                LOGE("%s, app_gpu_handle_get failed\n", __func__);
                return;
            }

            ret = bk_flexa_isp_gpu_bond_start(&s_isp_gpu_bond, isp_handle, gpu_handle);
            if (ret != BK_OK) {
                LOGE("%s, bk_flexa_isp_gpu_bond_start failed, ret = %d\n", __func__, ret);
                return;
            }

#ifdef CONFIG_MDS_SNAPSHOT
            (void)bk_snapshot_sw_prepare();
#endif

            return;
        }
        if (argc >= 3 && argv[2] != NULL && os_strcmp(argv[2], "uvc") == 0) {
#ifdef CONFIG_USB_CAMERA
            bool want_h264e = false;
            uint16_t width = 1280;
            uint16_t height = 720;
            int arg_idx = 3;

            if (argc > arg_idx + 1 && argv[arg_idx] != NULL && argv[arg_idx + 1] != NULL &&
                os_strcmp(argv[arg_idx], "h264e") != 0) {
                width = (uint16_t)os_strtoul(argv[arg_idx], NULL, 10);
                height = (uint16_t)os_strtoul(argv[arg_idx + 1], NULL, 10);
                if (width == 0 || height == 0) {
                    LOGE("Usage: joint_test open uvc [width height] [h264e]\n");
                    return;
                }
                arg_idx += 2;
            }

            for (int i = arg_idx; i < argc; ++i) {
                if (argv[i] == NULL) {
                    continue;
                }
                if (os_strcmp(argv[i], "h264e") == 0) {
                    want_h264e = true;
                    continue;
                }
                LOGE("Usage: joint_test open uvc [width height] [h264e]\n");
                return;
            }

            joint_test_mipi_lcd_on();

            bk_jpeg_decode_ctlr_handle_t decode_handle = NULL;
            LOGI("joint_test open uvc %ux%u h264e=%d\n", width, height, want_h264e);
            camera_parameters_ext_t ext_parameters = {
                .camera_width = width,
                .camera_height = height,
                .camera_out_format = BK_IMAGE_FORMAT_MJPEG,
                .port = 1,
            };

            ret = app_uvc_turn_on(&ext_parameters);
            if (ret != BK_OK) {
                LOGE("%s, app_uvc_turn_on failed, ret = %d\n", __func__, ret);
                return;
            }

            ret = app_jpeg_decode_open(width, height, BK_IMAGE_FORMAT_MJPEG, 1);
            if (ret != BK_OK) {
                LOGE("%s, app_jpeg_decode_open failed, ret = %d\n", __func__, ret);
                return;
            }
            ret = app_jpeg_decode_get_handle(&decode_handle);
            if (ret != BK_OK) {
                LOGE("%s, app_jpeg_decode_get_handle failed, ret = %d\n", __func__, ret);
                return;
            }
            if (want_h264e) {
                ret = app_h264_encode_open(width, height);
                if (ret != BK_OK) {
                    LOGE("%s, app_h264_encode_open failed, ret = %d\n", __func__, ret);
                    return;
                }
                bk_h264_encode_ctlr_handle_t enc_handle = NULL;
                ret = app_h264_encode_get_handle(&enc_handle);
                if (ret != BK_OK || enc_handle == NULL) {
                    LOGE("%s, app_h264_encode_get_handle failed, ret = %d\n", __func__, ret);
                    return;
                }

                ret = bk_flexa_mjpegd_h264e_bond_start(&s_mjpegd_h264e_bond, decode_handle, enc_handle);
                if (ret != BK_OK) {
                    LOGE("%s, bk_flexa_mjpegd_h264e_bond_start failed, ret = %d\n", __func__, ret);
                    return;
                }
            }
            /* app_gpu_v2_turn_on 从全局 gpu_board 读旋转/缩放；MIPI 残留 1920x1080+scale 与 UVC 分辨率不一致，按当前 UVC 分辨率+LCD 重新写入 */
            {
                gpu_board_config_t gpu_board_uvc = {0};
                gpu_board_uvc.flexa.enable = true;
                gpu_board_uvc.flexa.degree = 90;
                gpu_board_uvc.flexa.src_width = width;
                gpu_board_uvc.flexa.src_height = height;
                gpu_board_uvc.flexa.dst_width = 1920;
                gpu_board_uvc.flexa.dst_height = 1080;
                gpu_board_uvc.flexa.src_format = BK_PIXEL_FORMAT_NV12;
                gpu_board_uvc.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
                gpu_board_uvc.flexa.dst_compress = true;
                gpu_board_uvc.flexa.scale = (width != 1920 || height != 1080);
                gpu_board_uvc.flexa.tess_width = 0;
                gpu_board_uvc.flexa.tess_height = 0;
                app_gpu_board_config_set(&gpu_board_uvc);
            }
            ret = app_gpu_v2_turn_on(width, height);
            if (ret != BK_OK) {
                LOGE("%s, app_gpu_v2_turn_on failed, ret = %d\n", __func__, ret);
                return;
            }
            bk_gpu_ctlr_handle_t gpu_handle = app_gpu_handle_get();
            if (gpu_handle == NULL) {
                LOGE("%s, app_gpu_handle_get failed\n", __func__);
                return;
            }

            ret = bk_flexa_mjpegd_gpu_bond_start(&s_mjpegd_gpu_joint_bond, decode_handle, gpu_handle);
            if (ret != BK_OK) {
                LOGE("%s, bk_flexa_mjpegd_gpu_bond_start failed, ret = %d\n", __func__, ret);
                return;
            }

#else
            LOGE("%s, open uvc: CONFIG_USB_CAMERA off\n", __func__);
#endif
            return;
        }
        LOGE("Usage: joint_test open mipi [720p|1080p] [fps] [h264e] | open uvc [width height] [h264e]\n");
        return;
    }
    if (argc >= 2 && argv[1] != NULL && os_strcmp(argv[1], "close") == 0)
    {
        if (argc >= 3 && argv[2] != NULL && os_strcmp(argv[2], "uvc") == 0) {
#ifdef CONFIG_USB_CAMERA
            ret = app_mipi_lcd_turn_off();
            if (ret != BK_OK) {
                LOGE("%s, app_mipi_lcd_turn_off failed, ret = %d\n", __func__, ret);
                return;
            }
            if (s_mjpegd_h264e_bond != NULL) {
                bk_flexa_mjpegd_h264e_bond_stop(s_mjpegd_h264e_bond);
                s_mjpegd_h264e_bond = NULL;
            }
            if (s_mjpegd_gpu_joint_bond != NULL) {
                bk_flexa_mjpegd_gpu_bond_stop(s_mjpegd_gpu_joint_bond);
                s_mjpegd_gpu_joint_bond = NULL;
            }
            ret = app_uvc_turn_off(1);
            if (ret != BK_OK) {
                LOGE("%s, app_uvc_turn_off failed, ret = %d\n", __func__, ret);
                return;
            }
            ret = app_h264_encode_close();
            if (ret != BK_OK) {
                LOGE("%s, app_h264_encode_close failed, ret = %d\n", __func__, ret);
                return;
            }
            ret = app_jpeg_decode_close();
            if (ret != BK_OK) {
                LOGE("%s, app_jpeg_decode_close failed, ret = %d\n", __func__, ret);
                return;
            }
            bk_gpu_ctlr_handle_t gpu_handle = app_gpu_handle_get();
            if (gpu_handle != NULL) {
                ret = app_gpu_turn_off(gpu_handle);
                if (ret != BK_OK) {
                    LOGE("%s, app_gpu_turn_off failed, ret = %d\n", __func__, ret);
                    return;
                }
            }
#else
            LOGE("%s, close uvc: CONFIG_USB_CAMERA off\n", __func__);
#endif
            return;
        }
        if (argc >= 3 && argv[2] != NULL && os_strcmp(argv[2], "mipi") == 0) {
            bool need_app_h264e_turn_off = (s_isp_h264e_bond != NULL);

            ret = app_mipi_lcd_turn_off();
            if (ret != BK_OK) {
                LOGE("%s, app_mipi_lcd_turn_off failed, ret = %d\n", __func__, ret);
                return;
            }
            if (s_isp_gpu_bond != NULL) {
                bk_flexa_isp_gpu_bond_stop(s_isp_gpu_bond);
                s_isp_gpu_bond = NULL;
            }
            if (s_isp_h264e_bond != NULL) {
                bk_flexa_isp_h264e_bond_stop(s_isp_h264e_bond);
                s_isp_h264e_bond = NULL;
            }
            bk_gpu_ctlr_handle_t gpu_handle = app_gpu_handle_get();
            if (gpu_handle != NULL) {
                ret = app_gpu_turn_off(gpu_handle);
                if (ret != BK_OK) {
                    LOGE("%s, app_gpu_turn_off failed, ret = %d\n", __func__, ret);
                    return;
                }
            }
            if (need_app_h264e_turn_off) {
                int h264_ret = app_h264e_turn_off();
                if (h264_ret != BK_OK) {
                    LOGE("%s, app_h264e_turn_off failed, ret = %d\n", __func__, h264_ret);
                    return;
                }
            }
            ret = app_isp_camera_turn_off();
            if (ret != BK_OK) {
                LOGE("%s, app_isp_camera_turn_off failed, ret = %d\n", __func__, ret);
                return;
            }
            return;
        }
        LOGE("Usage: joint_test close uvc|mipi\n");
        return;
    }
    if (CMD_CONTAIN("test")) {
        test_hdma();
        return;
    }
    LOGE("Usage: joint_test open mipi [720p|1080p] [fps] [h264e] | open uvc [width height] [h264e] | test | close uvc|mipi\n");
}


void cli_avdk_mds_uvc_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
#ifdef CONFIG_USB_CAMERA
    avdk_err_t ret = AVDK_ERR_GENERIC;
    if (os_strcmp(argv[1], "open") == 0)
    {
        if (argc < 6)
        {
            LOGE("Usage: uvc open <port> <width> <height> <format>\n");
            return;
        }
        uint8_t port = os_strtoul(argv[2], NULL, 10);
        uint16_t width = os_strtoul(argv[3], NULL, 10);
        uint16_t height = os_strtoul(argv[4], NULL, 10);
        uint16_t format = 0;
        if (CMD_CONTAIN("h264"))
        {
            format = 1;
        }
        else if (CMD_CONTAIN("mjpeg"))
        {
            format = 0;
        }
        else
        {
            LOGE("Usage: uvc open <port> <width> <height> <format>\n");
            return;
        }
        camera_parameters_ext_t config = {
            .camera_width = width,
            .camera_height = height,
            .port = port,
            .camera_out_format = format,
        };
        ret = app_uvc_turn_on(&config);
    }
    else if (os_strcmp(argv[1], "close") == 0)
    {
        if (argc < 3)
        {
            LOGE("Usage: uvc close <port>\n");
            return;
        }
        uint8_t port = os_strtoul(argv[2], NULL, 10);
        ret = app_uvc_turn_off(port);
    }
    else
    {
        LOGE("Usage: uvc open <port> <width> <height> <fps> <format> or uvc close\n");
        return;
    }

    if (ret != AVDK_ERR_OK)
    {
        LOGE("Failed to %s UVC, ret=%d\n", __func__, ret);
    } else {
        LOGI("Successfully %s UVC\n", __func__);
    }
#endif
}

void cli_avdk_mds_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
#ifdef CONFIG_INTEGRATION_DOORBELL
    avdk_err_t ret = AVDK_ERR_GENERIC;
    if (os_strcmp(argv[1], "set_transfer_port") == 0)
    {
        if (argc < 3)
        {
            LOGE("Usage: doorbell set_transfer_port <port>\n");
            return;
        }
        uint8_t port = os_strtoul(argv[2], NULL, 10);
        extern bk_err_t doorbell_devices_set_transfer_port(uint8_t port_id);
        ret = doorbell_devices_set_transfer_port(port);
    }
    else
    {
        LOGE("Usage: doorbell <command>\n");
        return;
    }

    if (ret != AVDK_ERR_OK)
    {
        LOGE("Failed to execute command: %s, ret=%d\n", argv[1], ret);
    }
    else
    {
        LOGI("Successfully execute command: %s\n", argv[1]);
    }
#endif
}

void cli_avdk_mds_audio_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc < 2 || argv[1] == NULL)
    {
        LOGE("Usage: audio <turn_on|turn_off>\n");
        return;
    }

#if CONFIG_VOICE_SERVICE
    if (os_strcmp(argv[1], "turn_on") == 0)
    {
        uint32_t aec = 1;
        uint32_t uac = 0;
        uint32_t fmt  = 2;
        uint32_t sample_rate = 8000;
        if (argc > 5 && argv[2] != NULL && argv[3] != NULL && argv[4] != NULL && argv[5] != NULL)
        {
            aec = os_strtoul(argv[2], NULL, 10);
            uac = os_strtoul(argv[3], NULL, 10);
            fmt = os_strtoul(argv[5], NULL, 10);
            sample_rate = os_strtoul(argv[4], NULL, 10);
        }
        cli_doorbell_audio_turn_on(aec, uac, sample_rate, fmt);
    }
    else if (os_strcmp(argv[1], "turn_off") == 0)
    {
        cli_doorbell_audio_turn_off();
    }
#endif
#if (CONFIG_ASR_SERVICE_WITH_MIC)
    else if (os_strcmp(argv[1], "asr_turn_on") == 0)
    {
        uint32_t aec = 1;
        uint32_t uac = 0;
        uint32_t sample_rate = 16000;
        uint8_t asr_en = 1;
        if (argc > 4)
        {
            aec         = os_strtoul(argv[2], NULL, 10);
            uac         = os_strtoul(argv[3], NULL, 10);
            sample_rate = os_strtoul(argv[4], NULL, 10);
            if (argv[5]) {
                asr_en = os_strtoul(argv[5], NULL, 10);
            }
        } else {
            //LOGE("Usage: audio asr_turn_on <aec> <uac> <sample_rate>\n");
            //return; // TODO: add default value
        }
        cli_doorbell_asr_turn_on(aec, uac, sample_rate, asr_en);
    }
    else if (os_strcmp(argv[1], "asr_turn_off") == 0)
    {
        cli_doorbell_asr_turn_off();
    }
#endif
    else
    {
        LOGE("Usage: audio <turn_on|turn_off>\n");
    }
}

static bk_h264_encode_ctlr_handle_t cli_mds_h264_get_handle(void)
{
    bk_h264_encode_ctlr_handle_t enc_handle =
        (bk_h264_encode_ctlr_handle_t)app_h264_encode_handle_get();

    if (enc_handle == NULL)
    {
        (void)app_h264_encode_get_handle(&enc_handle);
    }

    return enc_handle;
}

static void cli_avdk_mds_h264_qp_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2 || argv[1] == NULL)
    {
        LOGE("Usage: h264_qp get | fixed <i_qp> [p_qp] | bitrate <bps> [i_min i_max p_min p_max]\n");
        return;
    }

    bk_h264_encode_ctlr_handle_t enc_handle = cli_mds_h264_get_handle();
    if (enc_handle == NULL)
    {
        LOGE("h264 encoder is not running\n");
        return;
    }

    if (os_strcmp(argv[1], "get") == 0)
    {
        bk_h264_encode_rate_ctrl_t rate_ctrl = {0};
        avdk_err_t ret = bk_h264_encode_get_rate_ctrl(enc_handle, &rate_ctrl);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("h264_qp get failed, ret=%d\n", ret);
            return;
        }
        LOGI("h264_qp bitrate=%u i_range=[%u,%u] p_range=[%u,%u]\n",
             rate_ctrl.bitrate, rate_ctrl.qp_min_i, rate_ctrl.qp_max_i,
             rate_ctrl.qp_min_p, rate_ctrl.qp_max_p);
        return;
    }

    if (os_strcmp(argv[1], "set") == 0)
    {
        if (argc < 7 || argv[2] == NULL || argv[3] == NULL ||
            argv[4] == NULL || argv[5] == NULL || argv[6] == NULL)
        {
            LOGE("Usage: h264_qp set <bitrate> <i_min:0-51> <i_max:0-51> <p_min:0-51> <p_max:0-51>\n");
            return;
        }

        uint32_t bitrate = os_strtoul(argv[2], NULL, 10);
        uint32_t i_min = os_strtoul(argv[3], NULL, 10);
        uint32_t i_max = os_strtoul(argv[4], NULL, 10);
        uint32_t p_min = os_strtoul(argv[5], NULL, 10);
        uint32_t p_max = os_strtoul(argv[6], NULL, 10);

        if (i_min > 51 || i_max > 51 || p_min > 51 || p_max > 51 ||
            (i_min && i_max && i_min > i_max) || (p_min && p_max && p_min > p_max))
        {
            LOGE("invalid h264_qp config, bitrate=%u i=[%u,%u] p=[%u,%u]\n",
                 bitrate, i_min, i_max, p_min, p_max);
            return;
        }

        bk_h264_encode_rate_ctrl_t rate_ctrl = {
            .bitrate = bitrate,
            .qp_min_i = (uint8_t)i_min,
            .qp_max_i = (uint8_t)i_max,
            .qp_min_p = (uint8_t)p_min,
            .qp_max_p = (uint8_t)p_max,
        };

        avdk_err_t ret = bk_h264_encode_set_rate_ctrl(enc_handle, &rate_ctrl);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("h264_qp bitrate failed, ret=%d\n", ret);
            return;
        }

        LOGI("h264_qp set bitrate=%u i=[%u,%u] p=[%u,%u] ok\n",
             bitrate, i_min, i_max, p_min, p_max);
        return;
    }

    LOGE("Usage: h264_qp get | set <bitrate> <i_min> <i_max> <p_min> <p_max>\n");
}

#if defined(CONFIG_INTEGRATION_DOORBELL) && defined(CONFIG_MDS_SNAPSHOT)
#include "bk_snapshot.h"

static void cli_avdk_mds_snapshot_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    extern bk_err_t doorbell_isp_snapshot_sw_capture(bk_snapshot_image_t *out_image);
    extern bk_err_t doorbell_isp_snapshot_capture(bk_snapshot_image_t *out_image);
    extern bk_err_t doorbell_snapshot_save_to_sd(const bk_snapshot_image_t *image, char *path_out,
                                                 uint32_t path_len);

    bk_snapshot_image_t image = {0};
    bk_err_t ret = BK_OK;
    bool use_sw = true;
    bool save_to_sd = false;
    char saved_path[64] = {0};
    int i;

    if (argc < 2 || argv[1] == NULL || os_strcmp(argv[1], "capture") != 0)
    {
        LOGE("Usage: snapshot capture [sw|hw] [save]\r\n");
        return;
    }

    for (i = 2; i < argc; i++)
    {
        if (argv[i] == NULL)
        {
            continue;
        }
        if (os_strcmp(argv[i], "hw") == 0)
        {
            use_sw = false;
        }
        else if (os_strcmp(argv[i], "sw") == 0)
        {
            use_sw = true;
        }
        else if (os_strcmp(argv[i], "save") == 0)
        {
            save_to_sd = true;
        }
        else
        {
            LOGE("Usage: snapshot capture [sw|hw] [save]\r\n");
            return;
        }
    }

    if (use_sw)
    {
        ret = doorbell_isp_snapshot_sw_capture(&image);
    }
    else
    {
        ret = doorbell_isp_snapshot_capture(&image);
    }

    if (ret != BK_OK)
    {
        LOGE("snapshot %s capture failed, ret=%d\r\n", use_sw ? "sw" : "hw", ret);
        goto out;
    }

    bk_printf("[RESULT][PASS] snapshot %s ok, size=%u %ux%u\r\n",
              use_sw ? "sw" : "hw", image.size, image.width, image.height);

    if (save_to_sd)
    {
        ret = doorbell_snapshot_save_to_sd(&image, saved_path, sizeof(saved_path));
        if (ret != BK_OK)
        {
            LOGE("snapshot save to sd failed, ret=%d\r\n", ret);
        }
        else if (saved_path[0] != '\0')
        {
            bk_printf("[RESULT][PASS] snapshot saved to %s\r\n", saved_path);
        }
    }

out:
    bk_snapshot_image_release(&image);
}
#endif

static const struct cli_command s_devices_cli_commands[] =
{
    {"isp", "isp...", cli_avdk_mds_isp_cmd},
    {"display", "display...", cli_avdk_mds_display_cmd},
    {"joint_test", "joint_test open mipi [720p|1080p] [fps] [h264e] | open uvc [width height] [h264e] | test | close uvc|mipi", cli_avdk_mds_joint_test_cmd},
    {"uvc", "uvc...", cli_avdk_mds_uvc_cmd},
    {"doorbell", "doorbell...", cli_avdk_mds_cmd},
    {"audio", "audio...", cli_avdk_mds_audio_cmd},
    {"h264_qp", "h264_qp get | set <bitrate> <i_min> <i_max> <p_min> <p_max>", cli_avdk_mds_h264_qp_cmd},
#if defined(CONFIG_INTEGRATION_DOORBELL) && defined(CONFIG_MDS_SNAPSHOT)
    {"snapshot", "snapshot capture [sw|hw] [save]", cli_avdk_mds_snapshot_cmd},
#endif
};

#define DEVICES_CLI_CMD_CNT  (sizeof(s_devices_cli_commands) / sizeof(struct cli_command))

void devices_cli_init(void)
{
    cli_register_commands(s_devices_cli_commands, DEVICES_CLI_CMD_CNT);
}
