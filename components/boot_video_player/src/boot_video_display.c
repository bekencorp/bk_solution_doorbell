// Copyright 2024-2025 Beken
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

/*
 * Display path for the boot video player. Ported / trimmed from
 * projects/multimedia/video_player_example:
 *   - LCD turn on/off, panel size, DPU runtime pixel-format switch
 *     (video_player_common.c)
 *   - display worker thread + video decode-complete callback
 *     (video_play_callbacks.c)
 *   - GPU NV12 strip rotate (video_play_gpu_postprocess.c)
 *
 * The component owns the LCD: it turns the panel on before playback and the
 * orchestrator turns it off afterwards (see boot_video_player.c).
 */

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include <components/bk_display.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_hardware_ram.h>
#include <components/media_types.h>
#include <driver/hpdma.h>
#include "modules/vg_lite_gpu/vg_lite.h"
#include "soc/reg_base.h"

#include "boot_video_priv.h"
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
#include <components/bk_video_player/video_decoder/bk_video_player_hw_h264_decoder.h>
#endif

/*
 * Keep the DPU decompress/format contract in lockstep with the HW H.264 GPU
 * output. Default matches the example (compressed ARGB8888 for the flexa path).
 */
#ifndef BOOT_VIDEO_H264_FLEXA_RAW_ARGB8888_ENABLE
#define BOOT_VIDEO_H264_FLEXA_RAW_ARGB8888_ENABLE 0
#endif

/* HX8399C panel geometry (portrait). Used to size the GPU rotate output. */
#define BOOT_VIDEO_GPU_PANEL_WIDTH      1080U
#define BOOT_VIDEO_GPU_PANEL_HEIGHT     1920U

extern void bk_gpu_driver_init(void);
extern void bk_gpu_driver_deinit(void);

/* ======================================================================== *
 *  LCD turn on/off + DPU runtime format
 * ======================================================================== */

static avdk_err_t boot_video_lcd_apply_video_format(bk_display_ctlr_handle_t handle,
                                                    boot_video_lcd_fmt_t fmt)
{
    if (handle == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    bk_display_pixel_format_config_t cfg;
    os_memset(&cfg, 0, sizeof(cfg));

    switch (fmt)
    {
    case BOOT_VIDEO_LCD_FMT_RGB888_RAW:
        cfg.format = BK_PIXEL_FORMAT_RGB888;
        cfg.decompress = false;
        break;
    case BOOT_VIDEO_LCD_FMT_ARGB8888_RAW:
        cfg.format = BK_PIXEL_FORMAT_ARGB8888;
        cfg.decompress = false;
        break;
    case BOOT_VIDEO_LCD_FMT_ARGB8888_COMPRESSED:
        cfg.format = BK_PIXEL_FORMAT_ARGB8888;
        cfg.decompress = true;
        break;
    case BOOT_VIDEO_LCD_FMT_RGB565_RAW:
        cfg.format = BK_PIXEL_FORMAT_RGB565;
        cfg.decompress = false;
        break;
    case BOOT_VIDEO_LCD_FMT_NV12_RAW:
    default:
        cfg.format = BK_PIXEL_FORMAT_NV12;
        cfg.decompress = false;
        break;
    }

    avdk_err_t ret = bk_display_pixel_format_set(handle, &cfg);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_VIDEO_LOGE("%s: DPU runtime format switch failed, fmt=%d ret=%d\n", __func__, fmt, ret);
    }
    return ret;
}

boot_video_lcd_fmt_t boot_video_lcd_format_for_video_codec(video_player_video_format_t format)
{
    /* H264 uses the frame-zerocopy decoder, which emits raw NV12 frames (no
     * DEC400 compression). The display worker still re-syncs the DPU format at
     * runtime from the actual decoded pixel format, but opening the LCD in the
     * matching format up front avoids a redundant format switch on frame 1. */
    (void)format;
    return BOOT_VIDEO_LCD_FMT_NV12_RAW;
}

avdk_err_t boot_video_lcd_apply_format(bk_display_ctlr_handle_t handle, boot_video_lcd_fmt_t fmt)
{
    return boot_video_lcd_apply_video_format(handle, fmt);
}

avdk_err_t boot_video_lcd_open_with_format(const boot_video_display_ops_t *ops,
                                           bk_display_ctlr_handle_t *out_handle,
                                           boot_video_lcd_fmt_t fmt)
{
    if (ops == NULL || ops->lcd_open == NULL)
    {
        BOOT_VIDEO_LOGE("%s: no display ops provided\n", __func__);
        return AVDK_ERR_INVAL;
    }

    /* Panel power + controller-handle acquisition is delegated to the caller
     * (dependency inversion), so this component does not depend on any board/app
     * display service. The backing owner is expected to be idempotent. */
    bk_display_ctlr_handle_t handle = NULL;
    bk_err_t ret = ops->lcd_open(ops->user, &handle);
    if (ret != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: ops->lcd_open failed, ret=%d\n", __func__, ret);
        return AVDK_ERR_GENERIC;
    }
    if (handle == NULL)
    {
        BOOT_VIDEO_LOGE("%s: ops->lcd_open returned NULL handle\n", __func__);
        return AVDK_ERR_GENERIC;
    }

    /* Bring the DPU video layer in line with what the decoder will produce.
     * Non-fatal: LCD is up either way (expect garbled frames on failure). */
    (void)boot_video_lcd_apply_video_format(handle, fmt);

    if (out_handle != NULL)
    {
        *out_handle = handle;
    }
    return AVDK_ERR_OK;
}

/* ======================================================================== *
 *  Rotation state
 * ======================================================================== */

static boot_video_lcd_fmt_t s_runtime_lcd_fmt = BOOT_VIDEO_LCD_FMT_NV12_RAW;
static bool s_runtime_lcd_fmt_valid = false;
static boot_video_rotate_mode_t s_video_rotate_mode = BOOT_VIDEO_ROTATE_NONE;

/* forward decl (GPU teardown, defined below) */
static void boot_video_gpu_postprocess_deinit(void);

void boot_video_lcd_runtime_format_reset(void)
{
    s_runtime_lcd_fmt_valid = false;
    boot_video_gpu_postprocess_deinit();
}

void boot_video_video_set_rotate_mode(boot_video_rotate_mode_t mode)
{
    s_video_rotate_mode = mode;
    boot_video_lcd_runtime_format_reset();
}

uint32_t boot_video_video_get_rotate_degree(void)
{
    if (s_video_rotate_mode == BOOT_VIDEO_ROTATE_90)
    {
        return 90U;
    }
    if (s_video_rotate_mode == BOOT_VIDEO_ROTATE_270)
    {
        return 270U;
    }
    return 0U;
}

avdk_err_t boot_video_lcd_close(const boot_video_display_ops_t *ops)
{
    boot_video_lcd_runtime_format_reset();

    if (ops == NULL || ops->lcd_close == NULL)
    {
        /* Nothing to power down (KEEP_ON/ASSUME_ON never reach here, and a caller
         * that omits lcd_close simply keeps the panel on). */
        return AVDK_ERR_OK;
    }
    return (ops->lcd_close(ops->user) == BK_OK) ? AVDK_ERR_OK : AVDK_ERR_GENERIC;
}

/* ======================================================================== *
 *  GPU NV12 strip rotate (VG-Lite + HPDMA), ported from
 *  video_play_gpu_postprocess.c
 * ======================================================================== */

#define BOOT_VIDEO_GPU_ALIGN_BYTES      64U
#define BOOT_VIDEO_GPU_PAD_BYTES        128U
#define BOOT_VIDEO_GPU_FLEXA_LINES      16U
#define BOOT_VIDEO_GPU_HPDMA_TIMEOUT_MS 3000U

typedef struct
{
    void    *data;
    uint32_t size;
    uint16_t visible_width;
    uint16_t visible_height;
    uint16_t render_width;
    uint16_t render_height;
} boot_video_gpu_frame_t;

typedef struct
{
    uintptr_t pingpong_raw;
    uintptr_t buffers[2];
    uint32_t strip_bytes;
    uint8_t dst_idx;
    hpdma_id_t gdma;
    void *link_table;
    beken_semaphore_t transfer_sem;
} boot_video_gpu_dma_t;

static bool s_gpu_initialized = false;
static void *s_gpu_contiguous_buffer = NULL;
static beken_mutex_t s_gpu_mutex = NULL;
static boot_video_gpu_dma_t s_gpu_dma = {
    .gdma = HPDMA_ID_MAX,
};

static inline uint32_t boot_video_gpu_align_up(uint32_t value, uint32_t align)
{
    return (value + align - 1U) & ~(align - 1U);
}

static inline uintptr_t boot_video_gpu_align_ptr(uintptr_t value, uintptr_t align)
{
    return (value + align - 1U) & ~(align - 1U);
}

static uint32_t boot_video_gpu_compressed_argb_size(uint32_t width, uint32_t height)
{
    /* DEC400 HV-sampled ARGB8888 stores width / 4 physical pixels per row. */
    return bk_pixel_size_get(BK_PIXEL_FORMAT_ARGB8888) * (width / 4U) * height;
}

static avdk_err_t boot_video_gpu_lock(void)
{
    if (s_gpu_mutex == NULL)
    {
        if (rtos_init_mutex(&s_gpu_mutex) != BK_OK)
        {
            BOOT_VIDEO_LOGE("%s: init mutex failed\n", __func__);
            return AVDK_ERR_GENERIC;
        }
    }
    rtos_lock_mutex(&s_gpu_mutex);
    return AVDK_ERR_OK;
}

static void boot_video_gpu_unlock(void)
{
    if (s_gpu_mutex != NULL)
    {
        rtos_unlock_mutex(&s_gpu_mutex);
    }
}

static void boot_video_gpu_dma_finish_cb(hpdma_id_t hpdma_id, void *user_data)
{
    (void)hpdma_id;
    if (user_data != NULL)
    {
        rtos_set_semaphore((beken_semaphore_t *)user_data);
    }
}

static void boot_video_gpu_dma_wait_idle(void)
{
    if (s_gpu_dma.gdma >= HPDMA_ID_MAX)
    {
        return;
    }

    for (uint32_t wait_ms = 0; wait_ms < BOOT_VIDEO_GPU_HPDMA_TIMEOUT_MS; wait_ms++)
    {
        if (bk_hpdma_get_next_ll_addr(s_gpu_dma.gdma) == 0U &&
            bk_hpdma_get_enable_status(s_gpu_dma.gdma) == 0U)
        {
            return;
        }
        rtos_delay_milliseconds(1);
    }

    BOOT_VIDEO_LOGE("%s: ch%d still busy before deinit\n", __func__, (int)s_gpu_dma.gdma);
}

static void boot_video_gpu_dma_deinit(void)
{
    if (s_gpu_dma.gdma < HPDMA_ID_MAX)
    {
        boot_video_gpu_dma_wait_idle();
        (void)bk_hpdma_disable_finish_interrupt(s_gpu_dma.gdma);
        (void)bk_hpdma_register_isr(s_gpu_dma.gdma, NULL, NULL, NULL, NULL);
        bk_err_t free_ret = bk_hpdma_free(HPDMA_DEV_DTCM, s_gpu_dma.gdma);
        if (free_ret == BK_ERR_HPDMA_TIMEOUT)
        {
            BOOT_VIDEO_LOGE("%s: ch%d free timeout, force reclaim\n", __func__, (int)s_gpu_dma.gdma);
            (void)bk_hpdma_force_reclaim(HPDMA_DEV_DTCM, s_gpu_dma.gdma);
        }
        else if (free_ret != BK_OK)
        {
            BOOT_VIDEO_LOGE("%s: ch%d free failed, ret=%d\n", __func__, (int)s_gpu_dma.gdma, (int)free_ret);
        }
        s_gpu_dma.gdma = HPDMA_ID_MAX;
    }

    if (s_gpu_dma.link_table != NULL)
    {
        bk_hpdma_link_deinit(s_gpu_dma.link_table);
        s_gpu_dma.link_table = NULL;
    }

    if (s_gpu_dma.transfer_sem != NULL)
    {
        (void)rtos_deinit_semaphore(&s_gpu_dma.transfer_sem);
        s_gpu_dma.transfer_sem = NULL;
    }

    if (s_gpu_dma.pingpong_raw != 0U)
    {
        hsram_free((void *)s_gpu_dma.pingpong_raw);
        s_gpu_dma.pingpong_raw = 0U;
    }

    s_gpu_dma.buffers[0] = 0U;
    s_gpu_dma.buffers[1] = 0U;
    s_gpu_dma.strip_bytes = 0U;
    s_gpu_dma.dst_idx = 0U;
}

static avdk_err_t boot_video_gpu_dma_init(uint32_t strip_bytes)
{
    const uint32_t pingpong_size = (strip_bytes * 2U) + BOOT_VIDEO_GPU_ALIGN_BYTES;
    uintptr_t base = 0U;
    bk_err_t bk_ret;

    s_gpu_dma.gdma = HPDMA_ID_MAX;
    s_gpu_dma.link_table = NULL;
    s_gpu_dma.transfer_sem = NULL;
    s_gpu_dma.pingpong_raw = 0U;
    s_gpu_dma.strip_bytes = strip_bytes;
    s_gpu_dma.dst_idx = 0U;

    base = (uintptr_t)bk_get_gpu_output_buffer(pingpong_size);
    if (base == 0U)
    {
        BOOT_VIDEO_LOGE("%s: alloc strip ping-pong failed, size=%u\n", __func__, (unsigned)pingpong_size);
        return AVDK_ERR_NOMEM;
    }

    s_gpu_dma.pingpong_raw = base;
    os_memset((void *)base, 0, pingpong_size);
    s_gpu_dma.buffers[0] = boot_video_gpu_align_ptr(base, BOOT_VIDEO_GPU_ALIGN_BYTES);
    s_gpu_dma.buffers[1] = boot_video_gpu_align_ptr(base + strip_bytes, BOOT_VIDEO_GPU_ALIGN_BYTES);

    s_gpu_dma.link_table = bk_hpdma_link_init(1);
    if (s_gpu_dma.link_table == NULL)
    {
        BOOT_VIDEO_LOGE("%s: bk_hpdma_link_init failed\n", __func__);
        goto fail;
    }

    s_gpu_dma.gdma = bk_hpdma_alloc(HPDMA_DEV_DTCM);
    if (s_gpu_dma.gdma >= HPDMA_ID_MAX)
    {
        BOOT_VIDEO_LOGE("%s: bk_hpdma_alloc failed\n", __func__);
        goto fail;
    }

    (void)bk_hpdma_set_dest_burst_len(s_gpu_dma.gdma, HPDMA_BURST_LEN_INC16);
    (void)bk_hpdma_set_src_burst_len(s_gpu_dma.gdma, HPDMA_BURST_LEN_INC16);

    bk_ret = rtos_init_semaphore(&s_gpu_dma.transfer_sem, 1);
    if (bk_ret != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: init transfer_sem failed, ret=%d\n", __func__, (int)bk_ret);
        goto fail;
    }

    (void)bk_hpdma_register_isr(s_gpu_dma.gdma, NULL, NULL,
                                boot_video_gpu_dma_finish_cb, &s_gpu_dma.transfer_sem);
    (void)bk_hpdma_enable_finish_interrupt(s_gpu_dma.gdma);

    return AVDK_ERR_OK;

fail:
    boot_video_gpu_dma_deinit();
    return AVDK_ERR_GENERIC;
}

static avdk_err_t boot_video_gpu_dma_wait(void)
{
    if (s_gpu_dma.transfer_sem == NULL)
    {
        return AVDK_ERR_INVAL;
    }
    if (rtos_get_semaphore(&s_gpu_dma.transfer_sem, BOOT_VIDEO_GPU_HPDMA_TIMEOUT_MS) != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: strip dma wait timeout\n", __func__);
        return AVDK_ERR_TIMEOUT;
    }
    return AVDK_ERR_OK;
}

static void boot_video_gpu_dma_normalize_sem(void)
{
    if (s_gpu_dma.transfer_sem == NULL)
    {
        return;
    }
    while (rtos_get_semaphore(&s_gpu_dma.transfer_sem, BEKEN_NO_WAIT) == BK_OK)
    {
    }
}

static avdk_err_t boot_video_gpu_dma_transfer(uint32_t src_addr, void *frame, uint32_t offset,
                                              uint32_t xsize, uint32_t ysize, uint32_t dst_step)
{
    hpdma_link_config_t cfg;
    os_memset(&cfg, 0, sizeof(cfg));

    cfg.src_addr = src_addr;
    cfg.dst_addr = (uint32_t)((uintptr_t)frame + offset);
    cfg.src_xsize = (uint16_t)xsize;
    cfg.src_ysize = (uint16_t)ysize;
    cfg.dst_xsize = (uint16_t)xsize;
    cfg.dst_ysize = (uint16_t)ysize;
    cfg.src_step = 0;
    cfg.dst_step = (uint16_t)dst_step;
    cfg.finish_int_en = 1;
    cfg.half_finish_int_en = 0;

    boot_video_gpu_dma_normalize_sem();
    if (bk_hpdma_link_set_descs(s_gpu_dma.link_table, &cfg, 1) != BK_OK ||
        bk_hpdma_link_transfer(s_gpu_dma.gdma,
                               (void *)SOC_SRAM_PERI_ADDR((uintptr_t)s_gpu_dma.link_table)) != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: start strip dma failed\n", __func__);
        return AVDK_ERR_GENERIC;
    }

    return boot_video_gpu_dma_wait();
}

static void boot_video_gpu_postprocess_deinit_locked(void)
{
    if (!s_gpu_initialized)
    {
        return;
    }

    boot_video_gpu_dma_deinit();
    (void)vg_lite_close();
    bk_gpu_driver_deinit();

    if (s_gpu_contiguous_buffer != NULL)
    {
        hsram_free(s_gpu_contiguous_buffer);
        s_gpu_contiguous_buffer = NULL;
    }

    s_gpu_initialized = false;
}

static avdk_err_t boot_video_gpu_ensure_init(uint32_t strip_bytes)
{
    if (s_gpu_initialized)
    {
        if (s_gpu_dma.strip_bytes >= strip_bytes)
        {
            return AVDK_ERR_OK;
        }
        boot_video_gpu_postprocess_deinit_locked();
    }

    if (strip_bytes == 0U)
    {
        return AVDK_ERR_OK;
    }

    bk_gpu_driver_init();

    s_gpu_contiguous_buffer = bk_get_gpu_flexa_buffer(CONFIG_VG_LITE_GPU_CONTIGUOUS_MEM_SZ);
    if (s_gpu_contiguous_buffer == NULL)
    {
        BOOT_VIDEO_LOGE("%s: alloc VG-Lite contiguous buffer failed\n", __func__);
        bk_gpu_driver_deinit();
        return AVDK_ERR_NOMEM;
    }

    vg_lite_error_t vg_ret = vg_lite_set_buffer((uint8_t *)s_gpu_contiguous_buffer);
    if (vg_ret == VG_LITE_SUCCESS)
    {
        vg_ret = vg_lite_init(0, 0);
    }
    if (vg_ret != VG_LITE_SUCCESS)
    {
        BOOT_VIDEO_LOGE("%s: VG-Lite init failed, ret=%d\n", __func__, (int)vg_ret);
        hsram_free(s_gpu_contiguous_buffer);
        s_gpu_contiguous_buffer = NULL;
        bk_gpu_driver_deinit();
        return AVDK_ERR_GENERIC;
    }

    avdk_err_t ret = boot_video_gpu_dma_init(strip_bytes);
    if (ret != AVDK_ERR_OK)
    {
        (void)vg_lite_close();
        hsram_free(s_gpu_contiguous_buffer);
        s_gpu_contiguous_buffer = NULL;
        bk_gpu_driver_deinit();
        return ret;
    }

    s_gpu_initialized = true;
    return AVDK_ERR_OK;
}

static void boot_video_gpu_postprocess_deinit(void)
{
    if (s_gpu_mutex == NULL)
    {
        return;
    }
    if (boot_video_gpu_lock() != AVDK_ERR_OK)
    {
        return;
    }
    boot_video_gpu_postprocess_deinit_locked();
    boot_video_gpu_unlock();
}

static void boot_video_gpu_set_strip_matrix(vg_lite_matrix_t *matrix,
                                            boot_video_rotate_mode_t rotate,
                                            uint32_t output_w, float scale_x, float scale_y,
                                            uint32_t strip_index)
{
    vg_lite_identity(matrix);

    if (rotate == BOOT_VIDEO_ROTATE_90)
    {
        const float strip_offset = (float)((strip_index - 1U) * BOOT_VIDEO_GPU_FLEXA_LINES);
        vg_lite_rotate(90.0f, matrix);
        vg_lite_scale(scale_x, -scale_y, matrix);
        matrix->m[0][2] = (vg_lite_float_t)(-strip_offset);
        matrix->m[1][2] = 0.0f;
    }
    else if (rotate == BOOT_VIDEO_ROTATE_270)
    {
        vg_lite_rotate(270.0f, matrix);
        vg_lite_scale(scale_x, -scale_y, matrix);
        matrix->m[0][2] = (vg_lite_float_t)(strip_index * BOOT_VIDEO_GPU_FLEXA_LINES);
        matrix->m[1][2] = (vg_lite_float_t)output_w;
    }
}

static avdk_err_t boot_video_gpu_nv12_rotate(const uint8_t *nv12, uint32_t width, uint32_t height,
                                             uint32_t stride, uint32_t y_plane_height,
                                             boot_video_rotate_mode_t rotate,
                                             boot_video_gpu_frame_t *out_frame)
{
    if (nv12 == NULL || out_frame == NULL || width == 0U || height == 0U ||
        stride < width || y_plane_height < height || ((width | height | stride | y_plane_height) & 1U) != 0U)
    {
        return AVDK_ERR_INVAL;
    }
    if (rotate != BOOT_VIDEO_ROTATE_90 && rotate != BOOT_VIDEO_ROTATE_270)
    {
        return AVDK_ERR_UNSUPPORTED;
    }

    uint32_t visible_w = BOOT_VIDEO_GPU_PANEL_WIDTH;
    uint32_t visible_h = BOOT_VIDEO_GPU_PANEL_HEIGHT;
    uint32_t render_w = BOOT_VIDEO_GPU_PANEL_HEIGHT; /* rotate swaps dimensions */
    uint32_t render_h = BOOT_VIDEO_GPU_PANEL_WIDTH;
    render_w = boot_video_gpu_align_up(render_w, 16U);
    render_h = boot_video_gpu_align_up(render_h, BOOT_VIDEO_GPU_FLEXA_LINES);
    const uint32_t strip_bytes = render_w * BOOT_VIDEO_GPU_FLEXA_LINES;
    const uint32_t frame_size = boot_video_gpu_compressed_argb_size(render_w, render_h);
    const uint32_t alloc_size = boot_video_gpu_align_up(frame_size + BOOT_VIDEO_GPU_PAD_BYTES,
                                                        BOOT_VIDEO_GPU_ALIGN_BYTES);

    avdk_err_t ret = boot_video_gpu_lock();
    if (ret != AVDK_ERR_OK)
    {
        return ret;
    }

    ret = boot_video_gpu_ensure_init(strip_bytes);
    if (ret != AVDK_ERR_OK)
    {
        boot_video_gpu_unlock();
        return ret;
    }

    void *gpu_frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, alloc_size);
    if (gpu_frame == NULL)
    {
        BOOT_VIDEO_LOGE("%s: alloc GPU output failed, size=%u\n", __func__, (unsigned)alloc_size);
        boot_video_gpu_unlock();
        return AVDK_ERR_NOMEM;
    }
    os_memset(gpu_frame, 0, alloc_size);

    vg_lite_buffer_t src_buf;
    vg_lite_buffer_t dst_buf;
    vg_lite_matrix_t matrix;
    os_memset(&src_buf, 0, sizeof(src_buf));
    os_memset(&dst_buf, 0, sizeof(dst_buf));
    os_memset(&matrix, 0, sizeof(matrix));

    src_buf.width = (vg_lite_uint32_t)width;
    src_buf.height = (vg_lite_uint32_t)height;
    src_buf.stride = (vg_lite_int32_t)stride;
    src_buf.format = VG_LITE_NV12;
    src_buf.compress_mode = VG_LITE_DEC_DISABLE;
    src_buf.tiled = VG_LITE_LINEAR;
    src_buf.yuv.uv_stride = (vg_lite_uint32_t)stride;
    src_buf.yuv.uv_height = (vg_lite_uint32_t)(y_plane_height / 2U);

    vg_lite_error_t vg_ret = vg_lite_allocate_with_data(&src_buf, (void *)nv12,
                                                        (void *)(nv12 + (stride * y_plane_height)),
                                                        NULL, NULL);
    if (vg_ret != VG_LITE_SUCCESS)
    {
        BOOT_VIDEO_LOGE("%s: wrap NV12 source failed, ret=%d\n", __func__, (int)vg_ret);
        bk_frame_buffer_free(gpu_frame);
        boot_video_gpu_unlock();
        return AVDK_ERR_GENERIC;
    }

    dst_buf.width = (vg_lite_uint32_t)BOOT_VIDEO_GPU_FLEXA_LINES;
    dst_buf.height = (vg_lite_uint32_t)render_w;
    dst_buf.format = VG_LITE_BGRA8888;
    dst_buf.compress_mode = VG_LITE_DEC_HV_SAMPLE;
    dst_buf.tiled = VG_LITE_TILED;
    vg_ret = vg_lite_allocate_with_data(&dst_buf, (void *)s_gpu_dma.buffers[s_gpu_dma.dst_idx],
                                        NULL, NULL, NULL);
    if (vg_ret != VG_LITE_SUCCESS)
    {
        BOOT_VIDEO_LOGE("%s: wrap GPU dst failed, ret=%d\n", __func__, (int)vg_ret);
        (void)vg_lite_free_without_free_data(&src_buf);
        bk_frame_buffer_free(gpu_frame);
        boot_video_gpu_unlock();
        return AVDK_ERR_GENERIC;
    }

    const uint32_t strip_count = render_h / BOOT_VIDEO_GPU_FLEXA_LINES;
    const uint32_t dma_xsize = BOOT_VIDEO_GPU_FLEXA_LINES * bk_pixel_size_get(BK_PIXEL_FORMAT_ARGB8888);
    const uint32_t dma_ysize = render_w / 4U;
    const uint32_t dma_dst_step = (render_h - BOOT_VIDEO_GPU_FLEXA_LINES) *
                                  bk_pixel_size_get(BK_PIXEL_FORMAT_ARGB8888);
    const float scale_x = (float)render_w / (float)width;
    const float scale_y = (float)render_h / (float)height;

    for (uint32_t strip_index = 1U; strip_index <= strip_count; strip_index++)
    {
        const uint32_t dma_offset = (rotate == BOOT_VIDEO_ROTATE_90)
                                    ? ((strip_index - 1U) * dma_xsize)
                                    : ((render_h - (strip_index * BOOT_VIDEO_GPU_FLEXA_LINES)) *
                                       bk_pixel_size_get(BK_PIXEL_FORMAT_ARGB8888));

        dst_buf.memory = (vg_lite_pointer)(uintptr_t)s_gpu_dma.buffers[s_gpu_dma.dst_idx];
        dst_buf.address = SOC_SRAM_PERI_ADDR(s_gpu_dma.buffers[s_gpu_dma.dst_idx]);
        boot_video_gpu_set_strip_matrix(&matrix, rotate, render_w, scale_x, scale_y, strip_index);

        vg_ret = vg_lite_blit(&dst_buf, &src_buf, &matrix, VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT);
        if (vg_ret == VG_LITE_SUCCESS)
        {
            vg_ret = vg_lite_finish();
        }
        if (vg_ret != VG_LITE_SUCCESS)
        {
            break;
        }

        ret = boot_video_gpu_dma_transfer((uint32_t)s_gpu_dma.buffers[s_gpu_dma.dst_idx],
                                          gpu_frame, dma_offset, dma_xsize, dma_ysize, dma_dst_step);
        if (ret != AVDK_ERR_OK)
        {
            break;
        }

        s_gpu_dma.dst_idx = 1U - s_gpu_dma.dst_idx;
    }

    (void)vg_lite_free_without_free_data(&dst_buf);
    (void)vg_lite_free_without_free_data(&src_buf);

    if (vg_ret != VG_LITE_SUCCESS || ret != AVDK_ERR_OK)
    {
        BOOT_VIDEO_LOGE("%s: strip rotate failed, vg_ret=%d ret=%d\n", __func__, (int)vg_ret, ret);
        bk_frame_buffer_free(gpu_frame);
        boot_video_gpu_unlock();
        return (ret != AVDK_ERR_OK) ? ret : AVDK_ERR_GENERIC;
    }

    out_frame->data = gpu_frame;
    out_frame->size = frame_size;
    out_frame->visible_width = (uint16_t)visible_w;
    out_frame->visible_height = (uint16_t)visible_h;
    out_frame->render_width = (uint16_t)render_w;
    out_frame->render_height = (uint16_t)render_h;
    boot_video_gpu_unlock();
    return AVDK_ERR_OK;
}

static avdk_err_t boot_video_gpu_free_frame(void *frame)
{
    if (frame != NULL)
    {
        bk_frame_buffer_free(frame);
    }
    return AVDK_ERR_OK;
}

/* ======================================================================== *
 *  Display worker + video decode-complete callback
 * ======================================================================== */

#define BOOT_VIDEO_DISPLAY_QUEUE_DEPTH        (1U)
#define BOOT_VIDEO_DISPLAY_WORKER_STACK_SIZE  (8 * 1024)
#define BOOT_VIDEO_DISPLAY_WORKER_PRIORITY    (BEKEN_DEFAULT_WORKER_PRIORITY)
#define BOOT_VIDEO_DISPLAY_POP_TIMEOUT_MS     (50U)

typedef struct
{
    void *pixel;                            /* owned raw decoded frame (pre-rotate) */
    uint32_t decoder_pixel_fmt;
    video_player_video_format_t video_format;
    uint16_t width;
    uint16_t height;
    boot_video_rotate_mode_t rotate_mode;
    bk_display_ctlr_handle_t lcd_handle;
} boot_video_display_node_t;

static beken_queue_t s_display_queue = NULL;
static beken_thread_t s_display_thread = NULL;
static beken_semaphore_t s_display_exit_sem = NULL;
static volatile bool s_display_worker_exit = false;
static bool s_display_worker_ready = false;

static avdk_err_t display_frame_free_cb(void *frame)
{
    bk_frame_buffer_free(frame);
    return AVDK_ERR_OK;
}

#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
static avdk_err_t display_h264_output_frame_free_cb(void *frame)
{
    return bk_video_player_hw_h264_decoder_free_output_frame(frame);
}
#endif

static void boot_video_free_output_pixel(uint32_t decoder_pixel_fmt, void *pixel)
{
    if (pixel == NULL)
    {
        return;
    }
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
    if (decoder_pixel_fmt == PIXEL_FMT_ARGB8888)
    {
        (void)bk_video_player_hw_h264_decoder_free_output_frame(pixel);
        return;
    }
#else
    (void)decoder_pixel_fmt;
#endif
    bk_frame_buffer_free(pixel);
}

static void boot_video_lcd_sync_format_for_output_frame(bk_display_ctlr_handle_t handle,
                                                        uint32_t display_pixel_fmt,
                                                        bool argb8888_compressed)
{
    if (handle == NULL)
    {
        return;
    }

    boot_video_lcd_fmt_t need = BOOT_VIDEO_LCD_FMT_NV12_RAW;
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
    if (display_pixel_fmt == PIXEL_FMT_ARGB8888)
    {
        need = argb8888_compressed ? BOOT_VIDEO_LCD_FMT_ARGB8888_COMPRESSED
                                   : BOOT_VIDEO_LCD_FMT_ARGB8888_RAW;
    }
    else if (display_pixel_fmt == PIXEL_FMT_RGB888)
    {
        need = BOOT_VIDEO_LCD_FMT_RGB888_RAW;
    }
    else if (display_pixel_fmt == PIXEL_FMT_RGB565)
    {
        need = BOOT_VIDEO_LCD_FMT_RGB565_RAW;
    }
#else
    (void)argb8888_compressed;
    if (display_pixel_fmt == PIXEL_FMT_RGB565)
    {
        need = BOOT_VIDEO_LCD_FMT_RGB565_RAW;
    }
    else if (display_pixel_fmt == PIXEL_FMT_RGB888)
    {
        need = BOOT_VIDEO_LCD_FMT_RGB888_RAW;
    }
#endif

    if (s_runtime_lcd_fmt_valid && need == s_runtime_lcd_fmt)
    {
        return;
    }

    if (boot_video_lcd_apply_format(handle, need) == AVDK_ERR_OK)
    {
        s_runtime_lcd_fmt = need;
        s_runtime_lcd_fmt_valid = true;
    }
}

static void boot_video_display_process_node(const boot_video_display_node_t *node)
{
    if (node == NULL || node->pixel == NULL)
    {
        return;
    }

    void *pixel = node->pixel;
    const uint32_t decoder_pixel_fmt = node->decoder_pixel_fmt;
    const boot_video_rotate_mode_t rotate_mode = node->rotate_mode;
    const bool h264_output_frame = (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264 &&
                                    (decoder_pixel_fmt == PIXEL_FMT_ARGB8888 ||
                                     decoder_pixel_fmt == PIXEL_FMT_RGB565));
    (void)h264_output_frame; /* only used when HW H264 decoder is enabled */
    uint32_t display_pixel_fmt = decoder_pixel_fmt;
    bool gpu_post_frame = false;

    if (node->lcd_handle == NULL)
    {
        boot_video_free_output_pixel(decoder_pixel_fmt, pixel);
        return;
    }

    if (rotate_mode != BOOT_VIDEO_ROTATE_NONE &&
        decoder_pixel_fmt == PIXEL_FMT_NV12 &&
        (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_MJPEG ||
         node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_H264))
    {
        const uint32_t src_w = node->width;
        const uint32_t src_h = node->height;
        const uint32_t src_stride = (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_MJPEG)
                                    ? ((src_w + 15U) & ~15U) : src_w;
        const uint32_t src_y_height = (node->video_format == VIDEO_PLAYER_VIDEO_FORMAT_MJPEG)
                                      ? ((src_h + 15U) & ~15U) : src_h;
        boot_video_gpu_frame_t gpu_frame;
        os_memset(&gpu_frame, 0, sizeof(gpu_frame));
        avdk_err_t rotate_ret = boot_video_gpu_nv12_rotate((const uint8_t *)pixel, src_w, src_h,
                                                           src_stride, src_y_height, rotate_mode, &gpu_frame);
        if (rotate_ret == AVDK_ERR_OK && gpu_frame.data != NULL)
        {
            boot_video_free_output_pixel(decoder_pixel_fmt, pixel);
            pixel = gpu_frame.data;
            display_pixel_fmt = PIXEL_FMT_ARGB8888;
            gpu_post_frame = true;
        }
        else
        {
            BOOT_VIDEO_LOGW("%s: GPU rotate failed, ret=%d; drop frame\n", __func__, rotate_ret);
            boot_video_free_output_pixel(decoder_pixel_fmt, pixel);
            return;
        }
    }

    bool display_argb8888_compressed = false;
    if (display_pixel_fmt == PIXEL_FMT_ARGB8888)
    {
#if BOOT_VIDEO_H264_FLEXA_RAW_ARGB8888_ENABLE
        display_argb8888_compressed = gpu_post_frame;
#else
        display_argb8888_compressed = true;
#endif
    }

    boot_video_lcd_sync_format_for_output_frame(node->lcd_handle, display_pixel_fmt,
                                                display_argb8888_compressed);

    avdk_err_t (*free_cb)(void *) = display_frame_free_cb;
    if (gpu_post_frame)
    {
        free_cb = boot_video_gpu_free_frame;
    }
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
    else if (h264_output_frame)
    {
        free_cb = display_h264_output_frame_free_cb;
    }
#endif

    avdk_err_t ret = bk_display_flush(node->lcd_handle, pixel, free_cb);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_VIDEO_LOGW("%s: bk_display_flush failed, ret=%d\n", __func__, ret);
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
        if (h264_output_frame)
        {
            (void)bk_video_player_hw_h264_decoder_free_output_frame(pixel);
        }
        else
#endif
        {
            (void)free_cb(pixel);
        }
    }
}

static void boot_video_display_node_discard(const boot_video_display_node_t *node)
{
    if (node != NULL && node->pixel != NULL)
    {
        boot_video_free_output_pixel(node->decoder_pixel_fmt, node->pixel);
    }
}

static void boot_video_display_worker_thread(void *arg)
{
    (void)arg;

    while (!s_display_worker_exit)
    {
        boot_video_display_node_t node;
        if (rtos_pop_from_queue(&s_display_queue, &node, BOOT_VIDEO_DISPLAY_POP_TIMEOUT_MS) == BK_OK)
        {
            boot_video_display_process_node(&node);
        }
    }

    /* drain leftovers without displaying (LCD about to be closed) */
    boot_video_display_node_t node;
    while (rtos_pop_from_queue(&s_display_queue, &node, BEKEN_NO_WAIT) == BK_OK)
    {
        boot_video_display_node_discard(&node);
    }

    rtos_set_semaphore(&s_display_exit_sem);
    rtos_delete_thread(NULL);
}

avdk_err_t boot_video_display_worker_init(void)
{
    if (s_display_worker_ready)
    {
        return AVDK_ERR_OK;
    }

    s_display_worker_exit = false;

    if (rtos_init_queue(&s_display_queue, "bv_disp_q",
                        sizeof(boot_video_display_node_t),
                        BOOT_VIDEO_DISPLAY_QUEUE_DEPTH) != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: init display queue failed\n", __func__);
        s_display_queue = NULL;
        return AVDK_ERR_NOMEM;
    }

    if (rtos_init_semaphore(&s_display_exit_sem, 1) != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: init display exit semaphore failed\n", __func__);
        rtos_deinit_queue(&s_display_queue);
        s_display_queue = NULL;
        return AVDK_ERR_NOMEM;
    }

    if (rtos_create_thread(&s_display_thread, BOOT_VIDEO_DISPLAY_WORKER_PRIORITY, "bv_display",
                           (beken_thread_function_t)boot_video_display_worker_thread,
                           BOOT_VIDEO_DISPLAY_WORKER_STACK_SIZE, NULL) != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: create display worker thread failed\n", __func__);
        rtos_deinit_semaphore(&s_display_exit_sem);
        s_display_exit_sem = NULL;
        rtos_deinit_queue(&s_display_queue);
        s_display_queue = NULL;
        return AVDK_ERR_GENERIC;
    }

    s_display_worker_ready = true;
    return AVDK_ERR_OK;
}

void boot_video_display_worker_deinit(void)
{
    if (!s_display_worker_ready)
    {
        return;
    }

    /* Caller guarantees the decode thread is already stopped (engine stop/close
     * joined it), so no more frames are being produced. */
    s_display_worker_exit = true;

    if (s_display_thread != NULL)
    {
        rtos_get_semaphore(&s_display_exit_sem, BEKEN_WAIT_FOREVER);
        s_display_thread = NULL;
    }

    if (s_display_exit_sem != NULL)
    {
        rtos_deinit_semaphore(&s_display_exit_sem);
        s_display_exit_sem = NULL;
    }

    if (s_display_queue != NULL)
    {
        rtos_deinit_queue(&s_display_queue);
        s_display_queue = NULL;
    }

    s_display_worker_ready = false;
}

void boot_video_video_decode_complete_cb(void *user_data,
                                         const video_player_video_frame_meta_t *meta,
                                         video_player_buffer_t *buffer)
{
    if (buffer == NULL || buffer->data == NULL)
    {
        return;
    }

    boot_video_ctx_t *ctx = (boot_video_ctx_t *)user_data;
    void *pixel = buffer->data;
    const uint32_t decoder_pixel_fmt = (meta != NULL) ? (uint32_t)meta->output_format : 0U;

    /* Transfer ownership out of the engine; null fields so buffer_free is a no-op. */
    buffer->data         = NULL;
    buffer->frame_buffer = NULL;
    buffer->length       = 0;

    if (ctx == NULL || ctx->lcd_handle == NULL ||
        !s_display_worker_ready || s_display_queue == NULL)
    {
        boot_video_free_output_pixel(decoder_pixel_fmt, pixel);
        return;
    }

    boot_video_display_node_t node;
    node.pixel            = pixel;
    node.decoder_pixel_fmt = decoder_pixel_fmt;
    node.video_format     = (meta != NULL) ? meta->video.format : VIDEO_PLAYER_VIDEO_FORMAT_UNKNOWN;
    node.width            = (meta != NULL) ? (uint16_t)meta->video.width : 0U;
    node.height           = (meta != NULL) ? (uint16_t)meta->video.height : 0U;
    node.rotate_mode      = s_video_rotate_mode;
    node.lcd_handle       = ctx->lcd_handle;

    /* Drop-oldest on full so the newest frame always wins (single producer). */
    if (rtos_is_queue_full(&s_display_queue))
    {
        boot_video_display_node_t old_node;
        if (rtos_pop_from_queue(&s_display_queue, &old_node, BEKEN_NO_WAIT) == BK_OK)
        {
            boot_video_display_node_discard(&old_node);
        }
    }

    if (rtos_push_to_queue(&s_display_queue, &node, BEKEN_NO_WAIT) != BK_OK)
    {
        BOOT_VIDEO_LOGW("%s: enqueue display frame failed, drop\n", __func__);
        boot_video_free_output_pixel(decoder_pixel_fmt, pixel);
    }
}
