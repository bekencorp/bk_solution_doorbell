#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/int.h>
#include <common/bk_err.h>
#include "sys_driver.h"
#include <components/bk_gpu_ctlr.h>
#include <components/bk_gpu.h>
#include "app_camera.h"
#include "app_display.h"
#include "app_gpu.h"
#include <avdk_check.h>
#include <components/bk_frame_buffer.h>
#include "app_display.h"
#include "app_jpeg_decode.h"
#include "driver/isp.h"
#include <driver/isp_types.h>
#if (CONFIG_PSRAM_WRITE_THROUGH)
#include <driver/psram_types.h>
#include <driver/psram.h>
#endif
#define TAG "app-gpu"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)


static bk_gpu_ctlr_handle_t s_gpu_handle = NULL;

gpu_board_config_t *gpu_board_config = NULL;

#if (CONFIG_PSRAM_WRITE_THROUGH)
static uint32_t s_psram_cover_area;   //todo: app_gpu_frame_malloc with params handler
#endif

static void *app_gpu_frame_malloc(uint32_t size)
{
    void *disp_frame = NULL;

    disp_frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);
    if (disp_frame == NULL)
    {
        LOGE("GPU failed to malloc frame\r\n");
        return NULL;
    }
#if (CONFIG_PSRAM_WRITE_THROUGH)
    if (bk_psram_enable_write_through(s_psram_cover_area, (uint32_t)disp_frame, (uint32_t)(disp_frame + size)) != BK_OK)
    {
        LOGE("Failed to enable write through\r\n");
        return NULL;
    }
#endif
    return disp_frame;
}
static avdk_err_t app_gpu_frame_free(void *ptr)
{
#if (CONFIG_PSRAM_WRITE_THROUGH)
    if (s_psram_cover_area < PSRAM_WRITE_THROUGH_AREA_COUNT)
    {
        bk_psram_disable_write_through(s_psram_cover_area);
    }
#endif
    if (ptr != NULL) {
        bk_frame_buffer_free(ptr);
    }
    return AVDK_ERR_OK;
}

static void app_gpu_frame_complete(void *frame, uint32_t frame_size, void *args)
{
    (void)frame_size;
    (void)args;
    avdk_err_t ret = app_mipi_lcd_flush(frame, app_gpu_frame_free);
    if (ret != AVDK_ERR_OK)
    {
        LOGD("%s, %d, GPU failed to complete frame %p, %d\r\n", __func__, __LINE__, frame, ret);
        app_gpu_frame_free(frame);
    }
}

avdk_err_t app_gpu_turn_on(gpu_board_config_t *config)
{
    bk_gpu_ctlr_config_t gpu_config;
    isp_control_t *isp_control = (isp_control_t *)app_isp_handle_get();
    if (isp_control == NULL) {
        LOGW("%s, %d, isp handle is NULL\n", __func__, __LINE__);
        return AVDK_ERR_GENERIC;
    }
    uint16_t src_width, src_height, dst_width, dst_height;
    uint8_t r_degree;

    if(isp_control == NULL) {
        LOGW("%s, %d, isp control is NULL, cannot open GPU first\n", __func__, __LINE__);
        return AVDK_ERR_NO_RESOURCE;
    }

    AVDK_RETURN_ON_FALSE((s_gpu_handle == NULL), AVDK_ERR_BUSY, TAG, "alread turned on");
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");
#if (CONFIG_VG_LITE_GPU)

    LOGI("%s: GPU config - src: %dx%d, dst: %dx%d, rotate: %d\n", 
         __func__, config->flexa.src_width, config->flexa.src_height, config->flexa.dst_width, config->flexa.dst_height, config->flexa.degree);


    os_memset(&gpu_config, 0, sizeof(bk_gpu_ctlr_config_t));
#if (CONFIG_PSRAM_WRITE_THROUGH)
    s_psram_cover_area = bk_psram_alloc_write_through_channel_with_psram_id(0);
    if (s_psram_cover_area >= PSRAM_WRITE_THROUGH_AREA_COUNT) {
        LOGE("alloc write-through channel failed\r\n");
        return AVDK_ERR_GENERIC;
    }
#endif
    gpu_config.rotate_degree = config->flexa.degree;
    gpu_config.src_width = config->flexa.src_width;
    gpu_config.src_height = config->flexa.src_height;
    gpu_config.dst_width = config->flexa.dst_width;;
    gpu_config.dst_height = config->flexa.dst_height;
    gpu_config.src_format = config->flexa.src_format;
    gpu_config.dst_format = config->flexa.dst_format;
    gpu_config.compress = config->flexa.dst_compress;
    gpu_config.scale = config->flexa.scale;
    gpu_config.tess_width = config->flexa.tess_width;
    gpu_config.tess_height = config->flexa.tess_height;
    gpu_config.src_buffer = (uint8_t *)(isp_control->chn[APP_ISP_MP_CHN_ID].y_addr);
    gpu_config.flexa_lines = 16;
    gpu_config.flexa_buff_cnt = isp_control->chn[APP_ISP_MP_CHN_ID].buf_cnt;
    gpu_config.flexa = true;
    gpu_config.frame_malloc = app_gpu_frame_malloc;
    gpu_config.frame_free = app_gpu_frame_free;
    gpu_config.flexa_line_done = NULL;
    gpu_config.flexa_line_done_args = NULL;
    gpu_config.frame_done = app_gpu_frame_complete;
    gpu_config.frame_done_args = NULL;

    avdk_err_t ret = bk_gpu_ctlr_new(&s_gpu_handle, &gpu_config);

    if (ret != BK_OK)
    {
        LOGW("%s, %d\n", __func__, __LINE__);
        return AVDK_ERR_GENERIC;
    }

    ret = bk_gpu_init(s_gpu_handle);

    if (ret != BK_OK)
    {
        LOGW("%s, %d\n", __func__, __LINE__);
        return AVDK_ERR_GENERIC;
    }

    ret = bk_gpu_open(s_gpu_handle);

    if (ret != BK_OK)
    {
        LOGW("%s, %d\n", __func__, __LINE__);
        return AVDK_ERR_GENERIC;
    }
#endif

    return AVDK_ERR_OK;
}

avdk_err_t app_gpu_turn_off(bk_gpu_ctlr_handle_t ctlr)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (ctlr == NULL)
    {
        LOGW("%s, %d, gpu handle is NULL\n", __func__, __LINE__);
        return AVDK_ERR_GENERIC;
    }

    ret = bk_gpu_close(ctlr);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("%s, %d, close gpu failed: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    ret = bk_gpu_deinit(ctlr);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("%s, %d, deinit gpu failed: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    ret = bk_gpu_delete(ctlr);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("%s, %d, delete gpu failed: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    if (ctlr == s_gpu_handle) {
        s_gpu_handle = NULL;
    }

#if (CONFIG_PSRAM_WRITE_THROUGH)
    bk_err_t wt_ret = bk_psram_free_write_through_channel((psram_write_through_area_t)s_psram_cover_area);
    if (wt_ret != BK_OK)
    {
        LOGW("%s, %d, free write-through channel failed: %d, area=%d\n", __func__, __LINE__, wt_ret, s_psram_cover_area);
        return AVDK_ERR_GENERIC;
    }
#endif

    return AVDK_ERR_OK;
}

static void flexa_line_done_cb(uint32_t done_lines, void *arg)
{
}

avdk_err_t app_gpu_v2_turn_on(uint16_t width, uint16_t height)
{
    avdk_err_t ret = AVDK_ERR_OK;
    bk_gpu_ctlr_config_t gpu_config;

    LOGI("%s, %d, width: %d, height: %d\n", __func__, __LINE__, width, height);

    os_memset(&gpu_config, 0, sizeof(bk_gpu_ctlr_config_t));

    display_board_config_t *display_board = app_display_board_config_get();
    gpu_board_config_t *gpu_board = app_gpu_board_config_get();

    if (display_board == NULL || gpu_board == NULL)
    {
        LOGW("%s board config %p, %p, %d\n", __func__, display_board, gpu_board, __LINE__);
        return AVDK_ERR_GENERIC;
    }

    gpu_config.rotate_degree = gpu_board->flexa.degree;
    gpu_config.src_width = width;
    gpu_config.src_height = height;

    if (gpu_config.rotate_degree == 90 || gpu_config.rotate_degree == 270)
    {
        gpu_config.dst_width = display_board->mipi.panel->timing.v_size;
        gpu_config.dst_height = display_board->mipi.panel->timing.h_size;
    }
    else {
        gpu_config.dst_width = display_board->mipi.panel->timing.h_size;
        gpu_config.dst_height = display_board->mipi.panel->timing.v_size;
    }
    gpu_config.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_config.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_config.compress = display_board->dpu_video.decompress;
    gpu_config.scale = gpu_board->flexa.scale;
    gpu_config.frame_malloc = app_gpu_frame_malloc;
    gpu_config.frame_free = app_gpu_frame_free;

    uint8_t *decode_buffer = NULL;
    uint8_t decode_buf_cnt = 0;
    ret = app_jpeg_decode_get_flexa_context(&decode_buffer, &decode_buf_cnt);
    if (ret != AVDK_ERR_OK) {
        LOGW("%s, %d get flexa context failed: %d\n", __func__, __LINE__, ret);
        return ret;
    }
    if (decode_buffer == NULL || decode_buf_cnt == 0) {
        LOGW("%s, %d decode buffer %p\n", __func__, __LINE__, decode_buffer);
        return AVDK_ERR_GENERIC;
    }

    gpu_config.src_buffer = decode_buffer;
    gpu_config.flexa = true;
    gpu_config.flexa_lines = 16;
    gpu_config.flexa_buff_cnt = decode_buf_cnt;
    gpu_config.flexa_line_done = flexa_line_done_cb;
    gpu_config.flexa_line_done_args = NULL;
    gpu_config.frame_done = app_gpu_frame_complete;
    gpu_config.frame_done_args = NULL;

    AVDK_RETURN_ON_FALSE((s_gpu_handle == NULL), AVDK_ERR_BUSY, TAG, "already turned on");

#if (CONFIG_PSRAM_WRITE_THROUGH)
    s_psram_cover_area = bk_psram_alloc_write_through_channel_with_psram_id(0);
    if (s_psram_cover_area >= PSRAM_WRITE_THROUGH_AREA_COUNT) {
        LOGE("%s alloc write-through channel failed\r\n", __func__);
        return AVDK_ERR_GENERIC;
    }
#endif

    ret = bk_gpu_ctlr_new(&s_gpu_handle, &gpu_config);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("%s, %d\n", __func__, __LINE__);
#if (CONFIG_PSRAM_WRITE_THROUGH)
        (void)bk_psram_free_write_through_channel((psram_write_through_area_t)s_psram_cover_area);
#endif
        return ret;
    }

    ret = bk_gpu_init(s_gpu_handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("%s, %d\n", __func__, __LINE__);
        (void)bk_gpu_delete(s_gpu_handle);
        s_gpu_handle = NULL;
#if (CONFIG_PSRAM_WRITE_THROUGH)
        (void)bk_psram_free_write_through_channel((psram_write_through_area_t)s_psram_cover_area);
#endif
        return ret;
    }

    ret = bk_gpu_open(s_gpu_handle);
    if (ret != AVDK_ERR_OK) {
        LOGW("%s, %d\n", __func__, __LINE__);
        (void)bk_gpu_deinit(s_gpu_handle);
        (void)bk_gpu_delete(s_gpu_handle);
        s_gpu_handle = NULL;
#if (CONFIG_PSRAM_WRITE_THROUGH)
        (void)bk_psram_free_write_through_channel((psram_write_through_area_t)s_psram_cover_area);
#endif
        return ret;
    }

    return ret;
}

bk_gpu_ctlr_handle_t app_gpu_handle_get(void)
{
    return s_gpu_handle;
}


int app_gpu_board_config_set(gpu_board_config_t *config)
{
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");

    if (gpu_board_config == NULL)
    {
        gpu_board_config = os_malloc(sizeof(gpu_board_config_t));
        AVDK_RETURN_ON_FALSE(gpu_board_config, AVDK_ERR_GENERIC, TAG, "gpu_board_config malloc failed");
    }

    os_memcpy(gpu_board_config, config, sizeof(gpu_board_config_t));

    return AVDK_ERR_OK;
}

gpu_board_config_t *app_gpu_board_config_get(void)
{
    return gpu_board_config;
}
