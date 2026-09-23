#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <common/avdk_pixel_types.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_hardware_ram.h>
#include <components/bk_gpu_ctlr.h>
#include <components/bk_gpu.h>
#if CONFIG_DOOR_ACCESS_CALL_FLOW
#include <components/bk_draw_osd.h>
#endif
#include <components/bk_isp_camera.h>
#include <driver/isp_base.h>
#include <driver/hpdma.h>
#include <driver/aon_rtc.h>
#if CONFIG_PSRAM_WRITE_THROUGH
#include <driver/psram.h>
#include <soc/reg_base.h>
#endif
#include "modules/vg_lite_gpu/vg_lite.h"

#include "app_display.h"
#include "app_gpu.h"
#include "app_camera.h"
#include "doorbell_devices.h"
#include "doorbell_isp_sp.h"
#include "doorbell_display_compositor.h"
#include "doorbell_downlink_video.h"

#define TAG "db-comp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define COMP_GPU_FLEXA_LINES     16U
/* The SDK GPU flex worker allocates the NEXT render target (frame_malloc) BEFORE
 * it flushes the just-finished frame to the DPU (see gpu_flex_data_frame_done in
 * bk_gpu_ctlr_default.c). So the pipeline holds up to 3 output frames at once:
 * 1 the GPU is rendering into + up to COMP_DISPLAY_PRIME_COUNT (2) still queued
 * at the DPU. So the peak is 3 concurrent output frames. With only 2 pool
 * buffers the 3rd came from a runtime malloc, which fails once the PIP self-view
 * pool has eaten the last MEM_SLAB_HEAP_UNCODED headroom -> frame_malloc returns
 * NULL, the worker keeps the same render buffer, never calls frame_done, and the
 * display freezes at 0fps. Reserving all 3 up front (at downlink start, when the
 * heap is emptiest) makes steady-state allocation never touch malloc. */
#define COMP_FRAME_POOL_COUNT    3U
#define COMP_DISPLAY_PRIME_COUNT 2U
#define COMP_DISPLAY_WAIT_MS     100U

/* Heap for the main GPU output (compressed-ARGB) frames the DPU scans out.
 * On PSRAM1 (MEM_SLAB_HEAP_CODED, 0x64000000) for the chip-split bandwidth win.
 * The PSRAM1 write-through cover is intentionally NOT opened (RCA
 * docs/psram_write_through_assert.md §13): comp_out_pool_init() does not call
 * comp_out_pool_cover_enable(). Decoder recon stays on PSRAM0 (UNCODED). */
#define COMP_OUT_HEAP            MEM_SLAB_HEAP_CODED

#define COMP_PIP_TASK_PRIORITY   5U
#define COMP_PIP_TASK_STACK      (1024U * 4U)
#define COMP_PIP_READ_TIMEOUT_MS 200U
#define COMP_PIP_POOL_COUNT      3U
#define COMP_PIP_PRIME_COUNT     2U
#define COMP_PIP_RELEASE_WAIT_MS 200U
/* Fail-safe: if the ISP SP self-view source is unavailable, give up on PIP
 * after this many consecutive read failures instead of spinning. This keeps a
 * broken PIP from ever starving the (higher-value) downlink decode/GPU path. */
#define COMP_PIP_MAX_FAIL        30U
#define COMP_PIP_FAIL_DELAY_MS   20U
/* Dedicated blit slot so PIP clear does not wipe bk_draw_osd overlay layers. */
#define COMP_PIP_BLIT_SLOT       0U
/* Decouple the self-view (PIP) refresh from the downlink frame rate: if no
 * downlink main frame has been flushed for this long, the main picture is
 * frozen, so the PIP task actively recomposes (frozen main background + a fresh
 * SP frame) onto a spare out_pool buffer and flushes it. Kept above a healthy
 * downlink frame interval so that, when downlink is fast enough, the normal
 * in-pipeline PIP blit path (gpu_flex_data_frame_done) handles it and this
 * supplemental refresh stays dormant.
 *
 * NOTE: the real steady-state downlink interval measured at runtime is ~66ms
 * (~13-15fps of composited output), so the previous 50ms value sat BELOW the
 * healthy interval and false-triggered the supplemental refresh 7-19x/5s,
 * burning a 2MB HPDMA copy + a GPU-lock overlay blit each time and stealing
 * cycles from the main decode+GPU bond. Raised to 200ms (~3 frame periods) so
 * it only fires on a genuine freeze. */
#define COMP_PIP_IDLE_MS         200U

typedef struct
{
    void *buf;
    uint8_t in_use;
} comp_pool_entry_t;

typedef struct
{
    bk_gpu_ctlr_handle_t gpu;
    volatile uint8_t running;
    uint8_t gpu_open_deferred;
    /* Product OSD registers once at boot; stop() must not clear gpu_event_cb. */
    doorbell_compositor_gpu_event_cb_t gpu_event_cb;
    uint8_t gpu_event_attached;

    uint16_t main_width;
    uint16_t main_height;

    /* GPU output (ARGB) frame pool -> DPU. */
    comp_pool_entry_t out_pool[COMP_FRAME_POOL_COUNT];
    uint32_t out_pool_size;
    uint32_t out_pool_count;
#if CONFIG_PSRAM_WRITE_THROUGH
    uint8_t out_pool_wt_on;
    uint8_t out_pool_wt_area;
#endif
    /* Main display output geometry (mirrors the GPU controller's compressed-ARGB
     * output), needed to describe the DPU-facing dst buffer to VG-Lite for the
     * project-side PIP-only blit. out_disp_w/h are the 16-line-aligned output
     * dims; out_rotate is the panel pre-rotation degree. */
    uint16_t out_disp_w;
    uint16_t out_disp_h;
    uint8_t out_rotate;
    beken_semaphore_t display_release_sem;
    volatile uint32_t display_pushed;
    /* The buffer the DPU is currently showing (last one flushed). It holds the
     * most recent composed main picture and stays pinned (in_use, not recycled)
     * until the next flush, so the idle PIP path can safely read it as the
     * background source. */
    void * volatile on_screen_frame;
    /* rtos_get_time() (ms) of the last DOWNLINK main-frame flush; drives the
     * "downlink idle" decision in the PIP task. Not updated by PIP-only flushes. */
    volatile uint32_t last_downlink_flush_ms;

    /* PIP overlay (ISP SP self-view). */
    bool pip_enable;
    volatile uint8_t pip_stop;   /* runtime request to stop the PIP task while the
                                  * main compositor keeps running (uplink/camera off) */
    uint16_t pip_w;
    uint16_t pip_h;
    uint16_t pip_dst_x;
    uint16_t pip_dst_y;
    uint16_t pip_rotate;
    uint32_t pip_frame_size;
    beken_thread_t pip_task;
    beken_semaphore_t pip_done_sem;
    beken_semaphore_t pip_release_sem;
    comp_pool_entry_t pip_pool[COMP_PIP_POOL_COUNT];
    uint32_t pip_pool_size;
    uint32_t pip_pool_count;

#if CONFIG_DOOR_ACCESS_CALL_FLOW
    /* Hang-up OSD via the public bk_draw_osd controller (indoor-display call-flow only).
     * video_intercom PIP uses bk_gpu_blit_set() on the same overlay, so this
     * handle is compiled out unless CONFIG_DOOR_ACCESS_CALL_FLOW (PIP off). */
    bk_draw_osd_ctlr_handle_t osd;
    /* Element table behind the OSD dynamic list. osd_assets[] is the
     * name -> descriptor table handed to bk_draw_osd_new, which keeps the
     * pointer for the instance lifetime; the dynamic list stores only the
     * osd_blend[] pointers, so replacing an element's pixels is an in-place
     * edit of its descriptor plus a dirty mark. */
    bk_blend_t   osd_blend[DOORBELL_OSD_SPRITE_MAX];
    blend_info_t osd_assets[DOORBELL_OSD_SPRITE_MAX + 1U];
    uint8_t      osd_elem_count;
#endif
} comp_ctx_t;

static comp_ctx_t s_comp = {0};

static void comp_notify_gpu_event(bool attached)
{
    if (s_comp.gpu_event_cb == NULL || s_comp.gpu == NULL)
    {
        return;
    }
    if (attached)
    {
        if (s_comp.gpu_event_attached != 0U)
        {
            return;
        }
        s_comp.gpu_event_attached = 1U;
        s_comp.gpu_event_cb(s_comp.gpu, true);
    }
    else
    {
        if (s_comp.gpu_event_attached == 0U)
        {
            return;
        }
        s_comp.gpu_event_attached = 0U;
        s_comp.gpu_event_cb(s_comp.gpu, false);
    }
}

/* ------------------------------------------------------------------ */
/* GPU output frame pool                                              */
/* ------------------------------------------------------------------ */

static void *comp_out_try_pool(uint32_t size)
{
    void *ptr = NULL;
    uint32_t flags;
    uint32_t i;

    flags = rtos_enter_critical();
    if (size == s_comp.out_pool_size && s_comp.out_pool_count != 0U)
    {
        for (i = 0; i < s_comp.out_pool_count; i++)
        {
            if (s_comp.out_pool[i].buf != NULL && s_comp.out_pool[i].in_use == 0U)
            {
                s_comp.out_pool[i].in_use = 1U;
                ptr = s_comp.out_pool[i].buf;
                break;
            }
        }
    }
    rtos_exit_critical(flags);
    return ptr;
}

static void *comp_out_alloc(uint32_t size)
{
    void *ptr = comp_out_try_pool(size);

    /* Rejected earlier attempt: block-waiting here for a pool buffer deadlocks at
     * pool<3 because the SDK worker allocs the next render target BEFORE flushing
     * (which is what would free a buffer). With COMP_FRAME_POOL_COUNT covering the
     * peak-3 need, the pool always has a free buffer in steady state, so this just
     * falls through to the pool. malloc stays as a transient safety net; a single
     * NULL only skips one GPU frame and self-heals once the DPU releases. */
    if (ptr == NULL)
    {
        /* Transient safety-net buffer (pool momentarily exhausted): keep it plain
         * cached - do not spend one of the 4 scarce write-through areas on a
         * short-lived frame. Still COMP_OUT_HEAP (PSRAM1): a main output frame on
         * PSRAM0 would share that chip with the decoder recon/reference reads and
         * cost ~6ms on every P frame (see the chip-split rationale above). */
        ptr = bk_frame_buffer_malloc(COMP_OUT_HEAP, size);
    }
    return ptr;
}

static avdk_err_t comp_out_free(void *ptr)
{
    uint32_t flags;
    uint8_t from_pool = 0U;
    uint32_t i;

    if (ptr == NULL)
    {
        return AVDK_ERR_OK;
    }

    flags = rtos_enter_critical();
    for (i = 0; i < s_comp.out_pool_count; i++)
    {
        if (s_comp.out_pool[i].buf == ptr)
        {
            s_comp.out_pool[i].in_use = 0U;
            from_pool = 1U;
            break;
        }
    }
    rtos_exit_critical(flags);

    if (from_pool == 0U)
    {
        /* The DPU is double-buffered: it defers a frame's release callback until
         * the NEXT flush is committed. The compositor's last displayed frame thus
         * keeps comp_dpu_release() latched in the DPU and it only fires when the
         * next owner (LVGL) commits its first frame - i.e. AFTER the compositor was
         * stopped and comp_out_pool_deinit() freed (and the slab recycled) that
         * buffer to LVGL. Freeing it again here would corrupt LVGL's live buffer.
         * s_comp.running is cleared at the very start of doorbell_compositor_stop(),
         * so treat any free that reaches this point post-teardown as a no-op. */
        if (s_comp.running == 0U)
        {
            return AVDK_ERR_OK;
        }
        bk_frame_buffer_free(ptr);
    }
    return AVDK_ERR_OK;
}

#if CONFIG_PSRAM_WRITE_THROUGH
#define COMP_OUT_POOL_WT_ALIGN 64U

static void comp_out_pool_cover_disable(void)
{
    if (s_comp.out_pool_wt_on == 0U)
    {
        return;
    }

    (void)bk_psram_disable_write_through((psram_write_through_area_t)s_comp.out_pool_wt_area);
    (void)bk_psram_free_write_through_channel((psram_write_through_area_t)s_comp.out_pool_wt_area);
    s_comp.out_pool_wt_on = 0U;
    s_comp.out_pool_wt_area = 0U;
}

static void comp_out_pool_cover_enable(uint32_t buf_size) __attribute__((unused));
static void comp_out_pool_cover_enable(uint32_t buf_size)
{
    uint32_t start = 0xFFFFFFFFu;
    uint32_t end = 0U;
    uint32_t i;
    psram_write_through_area_t area;

    comp_out_pool_cover_disable();

    for (i = 0U; i < s_comp.out_pool_count; i++)
    {
        uint32_t a;
        uint32_t b;

        if (s_comp.out_pool[i].buf == NULL || buf_size == 0U)
        {
            continue;
        }
        a = (uint32_t)(uintptr_t)s_comp.out_pool[i].buf;
        b = a + buf_size;
        if (a < start)
        {
            start = a;
        }
        if (b > end)
        {
            end = b;
        }
    }
    if (start >= end)
    {
        return;
    }

    start = (start + (COMP_OUT_POOL_WT_ALIGN - 1U)) & ~(COMP_OUT_POOL_WT_ALIGN - 1U);
    end &= ~(COMP_OUT_POOL_WT_ALIGN - 1U);
    if (start >= end)
    {
        return;
    }

    area = bk_psram_alloc_write_through_channel_with_psram_id(
        (start >= (uint32_t)SOC_PSRAM1_DATA_BASE) ? 1U : 0U);
    if (area >= PSRAM_WRITE_THROUGH_AREA_COUNT)
    {
        LOGE("out pool cover: no write-through channel\n");
        return;
    }
    if (bk_psram_enable_write_through(area, start, end) != BK_OK)
    {
        (void)bk_psram_free_write_through_channel(area);
        LOGE("out pool cover: enable 0x%08x-0x%08x failed\n", start, end);
        return;
    }
    s_comp.out_pool_wt_on = 1U;
    s_comp.out_pool_wt_area = (uint8_t)area;
    LOGI("out pool cover: write-through 0x%08x-0x%08x slots=%u area=%u\n",
         start, end, (unsigned)s_comp.out_pool_count, (unsigned)area);
}
#else
static void comp_out_pool_cover_disable(void)
{
}

static void comp_out_pool_cover_enable(uint32_t buf_size) __attribute__((unused));
static void comp_out_pool_cover_enable(uint32_t buf_size)
{
    (void)buf_size;
}
#endif

static avdk_err_t comp_out_pool_init(uint32_t buf_size)
{
    uint32_t i;

    if (buf_size == 0U)
    {
        return AVDK_ERR_INVAL;
    }

    for (i = 0; i < COMP_FRAME_POOL_COUNT; i++)
    {
        s_comp.out_pool[i].buf = bk_frame_buffer_malloc(COMP_OUT_HEAP, buf_size);
        s_comp.out_pool[i].in_use = 0U;
        if (s_comp.out_pool[i].buf == NULL)
        {
            uint32_t j;
            for (j = 0; j < i; j++)
            {
                bk_frame_buffer_free(s_comp.out_pool[j].buf);
                s_comp.out_pool[j].buf = NULL;
            }
            s_comp.out_pool_count = 0U;
            s_comp.out_pool_size = 0U;
            return AVDK_ERR_NOMEM;
        }
    }
    s_comp.out_pool_count = COMP_FRAME_POOL_COUNT;
    s_comp.out_pool_size = buf_size;

    /* Anti-regression guard for the PSRAM chip split. The GPU output pool (which
     * the DPU scans out) MUST stay on PSRAM1; the decoder recon/reference frames
     * live on PSRAM0. An SDK/solution sync once silently rewrote the allocation
     * below to MEM_SLAB_HEAP_UNCODED, putting the pool on PSRAM0 next to the
     * recon. The COMP_OUT_HEAP macro and its comment survived, so the bug was
     * invisible in review, and it cost ~2.3ms on every P frame (36fps -> 34fps:
     * the decoder's 1.38MB reference read then competes with the GPU write and
     * the DPU scan-out on one chip). Print the chip the pool actually landed on
     * so one glance at the boot log after any sync catches a repeat. */
    {
        uint32_t base = (uint32_t)(uintptr_t)s_comp.out_pool[0].buf;
        uint8_t chip = (base >= (uint32_t)SOC_PSRAM1_DATA_BASE) ? 1U : 0U;

        if (chip == 1U)
        {
            LOGI("out pool: %u x %u @0x%08x -> PSRAM%u (chip-split OK)\n",
                 (unsigned)COMP_FRAME_POOL_COUNT, (unsigned)buf_size,
                 (unsigned)base, (unsigned)chip);
        }
        else
        {
            LOGE("out pool: %u x %u @0x%08x -> PSRAM%u, EXPECTED PSRAM1! "
                 "chip split broken, downlink loses ~2ms/P-frame\n",
                 (unsigned)COMP_FRAME_POOL_COUNT, (unsigned)buf_size,
                 (unsigned)base, (unsigned)chip);
        }
    }

    /* PRODUCTION (docs/psram_write_through_assert.md §12.26): the GPU output pool
     * write-through cover on PSRAM1 is DISABLED. The pool stays on PSRAM1 (CODED)
     * for the chip-split bandwidth win, but opening a write-through window on the
     * chip that also holds the RTOS task stacks caused the crashes in this RCA;
     * 36fps does not depend on the cover. The cover helpers are kept (unused) for
     * reference only. */
    /* comp_out_pool_cover_enable(buf_size); */
    return AVDK_ERR_OK;
}

static void comp_out_pool_deinit(void)
{
    uint32_t i;

    comp_out_pool_cover_disable();

    for (i = 0; i < COMP_FRAME_POOL_COUNT; i++)
    {
        if (s_comp.out_pool[i].buf != NULL)
        {
            bk_frame_buffer_free(s_comp.out_pool[i].buf);
            s_comp.out_pool[i].buf = NULL;
            s_comp.out_pool[i].in_use = 0U;
        }
    }
    s_comp.out_pool_count = 0U;
    s_comp.out_pool_size = 0U;
}

static avdk_err_t comp_dpu_release(void *ptr)
{
    (void)comp_out_free(ptr);
    if (s_comp.display_release_sem != NULL)
    {
        (void)rtos_set_semaphore(&s_comp.display_release_sem);
    }
    return AVDK_ERR_OK;
}

/* Push a composed frame to the DPU. is_downlink distinguishes a real downlink
 * main frame (updates the idle-detection timestamp) from a supplemental PIP-only
 * refresh (must not, or the idle detection would never fire). On success the
 * frame becomes the pinned on-screen buffer. */
static void comp_display_push(void *frame, bool is_downlink)
{
    int ret;

    /* Back-pressure: don't outrun the DPU release rate. */
    if (s_comp.display_pushed >= COMP_DISPLAY_PRIME_COUNT &&
        s_comp.display_release_sem != NULL)
    {
        (void)rtos_get_semaphore(&s_comp.display_release_sem, COMP_DISPLAY_WAIT_MS);
    }

    ret = app_mipi_lcd_flush(frame, comp_dpu_release);
    if (ret != BK_OK)
    {
        LOGW("lcd flush failed=%d, drop frame\n", ret);
        (void)comp_out_free(frame);
        return;
    }
    s_comp.display_pushed++;
    s_comp.on_screen_frame = frame;
    if (is_downlink)
    {
        s_comp.last_downlink_flush_ms = (uint32_t)rtos_get_time();
    }
}

static void comp_frame_done(void *frame, uint32_t frame_size, void *args)
{
    (void)frame_size;
    (void)args;
    comp_display_push(frame, true);
}

/* ------------------------------------------------------------------ */
/* ISP SP -> GPU blit PIP overlay                                    */
/* ------------------------------------------------------------------ */

static void *comp_pip_alloc(uint32_t size)
{
    void *ptr = NULL;
    uint32_t flags;
    uint32_t i;

    flags = rtos_enter_critical();
    if (size == s_comp.pip_pool_size && s_comp.pip_pool_count != 0U)
    {
        for (i = 0; i < s_comp.pip_pool_count; i++)
        {
            if (s_comp.pip_pool[i].buf != NULL && s_comp.pip_pool[i].in_use == 0U)
            {
                s_comp.pip_pool[i].in_use = 1U;
                ptr = s_comp.pip_pool[i].buf;
                break;
            }
        }
    }
    rtos_exit_critical(flags);

    if (ptr == NULL)
    {
        ptr = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);
    }
    return ptr;
}

static void comp_pip_free(void *ptr)
{
    uint32_t flags;
    uint8_t from_pool = 0U;
    uint32_t i;

    if (ptr == NULL)
    {
        return;
    }

    flags = rtos_enter_critical();
    for (i = 0; i < s_comp.pip_pool_count; i++)
    {
        if (s_comp.pip_pool[i].buf == ptr)
        {
            s_comp.pip_pool[i].in_use = 0U;
            from_pool = 1U;
            break;
        }
    }
    rtos_exit_critical(flags);

    if (from_pool == 0U)
    {
        bk_frame_buffer_free(ptr);
    }
}

static avdk_err_t comp_pip_pool_init(uint32_t buf_size)
{
    uint32_t i;

    if (buf_size == 0U)
    {
        return AVDK_ERR_INVAL;
    }
    for (i = 0; i < COMP_PIP_POOL_COUNT; i++)
    {
        /* PIP self-view is small and low-value; use plain cached buffers so the 4
         * scarce write-through areas stay reserved for the main out_pool. */
        s_comp.pip_pool[i].buf = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, buf_size);
        s_comp.pip_pool[i].in_use = 0U;
        if (s_comp.pip_pool[i].buf == NULL)
        {
            uint32_t j;
            for (j = 0; j < i; j++)
            {
                bk_frame_buffer_free(s_comp.pip_pool[j].buf);
                s_comp.pip_pool[j].buf = NULL;
            }
            s_comp.pip_pool_count = 0U;
            s_comp.pip_pool_size = 0U;
            return AVDK_ERR_NOMEM;
        }
    }
    s_comp.pip_pool_count = COMP_PIP_POOL_COUNT;
    s_comp.pip_pool_size = buf_size;
    return AVDK_ERR_OK;
}

static void comp_pip_pool_deinit(void)
{
    uint32_t i;

    for (i = 0; i < COMP_PIP_POOL_COUNT; i++)
    {
        if (s_comp.pip_pool[i].buf != NULL)
        {
            bk_frame_buffer_free(s_comp.pip_pool[i].buf);
            s_comp.pip_pool[i].buf = NULL;
            s_comp.pip_pool[i].in_use = 0U;
        }
    }
    s_comp.pip_pool_count = 0U;
    s_comp.pip_pool_size = 0U;
}

static void comp_pip_blit_free_cb(void *frame, void *args)
{
    (void)args;
    comp_pip_free(frame);
    if (s_comp.pip_release_sem != NULL)
    {
        (void)rtos_set_semaphore(&s_comp.pip_release_sem);
    }
}

/* True when the downlink main picture has not been flushed for COMP_PIP_IDLE_MS,
 * i.e. it is frozen and the SDK in-pipeline PIP blit path (which only runs on a
 * downlink frame_done) is dormant. */
static bool comp_downlink_idle(void)
{
    return (uint32_t)((uint32_t)rtos_get_time() - s_comp.last_downlink_flush_ms) >= COMP_PIP_IDLE_MS;
}

/* Project-side PIP overlay blit (equivalent to the SDK's bk_gpu_blit_render /
 * gpu_flex_data_frame_done_blit, kept here so the bk_gpu component stays stock).
 *
 * Blits one NV12 self-view frame (sp_frame) onto dst_bg, which already holds the
 * frozen main picture in the DPU-facing compressed-ARGB layout. Runs under the
 * GPU controller's own gpu_mutex (BK_GPU_IOCTL_LOCK) so it is serialized against
 * the controller's flex worker VG-Lite usage - the controller shares a single
 * VG-Lite instance which is not internally thread-safe. Does NOT flush; the
 * caller pushes dst_bg to the display. */
static avdk_err_t comp_pip_blit_overlay(void *dst_bg, void *sp_frame)
{
    vg_lite_buffer_t dst;
    vg_lite_buffer_t src;
    vg_lite_matrix_t m;
    vg_lite_rectangle_t rect;
    avdk_err_t ret = AVDK_ERR_OK;
    int vret;

    if (dst_bg == NULL || sp_frame == NULL || s_comp.gpu == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    /* dst: DPU-facing compressed-ARGB output (BGRA8888 tiled DEC_HV_SAMPLE),
     * width/height taken from the controller output dims with a 90/270 swap. */
    os_memset(&dst, 0, sizeof(dst));
    if (s_comp.out_rotate == 90U || s_comp.out_rotate == 270U)
    {
        dst.width  = s_comp.out_disp_h;
        dst.height = s_comp.out_disp_w;
    }
    else
    {
        dst.width  = s_comp.out_disp_w;
        dst.height = s_comp.out_disp_h;
    }
    dst.format        = VG_LITE_BGRA8888;
    dst.tiled         = VG_LITE_TILED;
    dst.compress_mode = VG_LITE_DEC_HV_SAMPLE;

    /* src: local self-view NV12 frame (Y plane then interleaved UV at w*h). */
    os_memset(&src, 0, sizeof(src));
    src.width  = s_comp.pip_w;
    src.height = s_comp.pip_h;
    src.format = VG_LITE_NV12;

    if (bk_gpu_ioctl(s_comp.gpu, BK_GPU_IOCTL_LOCK, NULL) != AVDK_ERR_OK)
    {
        return AVDK_ERR_GENERIC;
    }

    vg_lite_allocate_with_data(&src, sp_frame,
                               (uint8_t *)sp_frame +
                                   ((uint32_t)s_comp.pip_w * (uint32_t)s_comp.pip_h),
                               NULL, NULL);
    vg_lite_allocate_with_data(&dst, dst_bg, NULL, NULL, NULL);

    os_memset(&m, 0, sizeof(m));
    vg_lite_identity(&m);
    switch (s_comp.pip_rotate)
    {
        case 90:
            vg_lite_rotate(90.0f, &m);
            m.m[0][2] = (vg_lite_float_t)s_comp.pip_dst_x + (vg_lite_float_t)s_comp.pip_h;
            m.m[1][2] = (vg_lite_float_t)s_comp.pip_dst_y;
            break;
        case 180:
            vg_lite_rotate(180.0f, &m);
            m.m[0][2] = (vg_lite_float_t)s_comp.pip_dst_x + (vg_lite_float_t)s_comp.pip_w;
            m.m[1][2] = (vg_lite_float_t)s_comp.pip_dst_y + (vg_lite_float_t)s_comp.pip_h;
            break;
        case 270:
            vg_lite_rotate(270.0f, &m);
            m.m[0][2] = (vg_lite_float_t)s_comp.pip_dst_x;
            m.m[1][2] = (vg_lite_float_t)s_comp.pip_dst_y + (vg_lite_float_t)s_comp.pip_w;
            break;
        case 0:
        default:
            vg_lite_translate(s_comp.pip_dst_x, s_comp.pip_dst_y, &m);
            break;
    }

    rect.x      = 0;
    rect.y      = 0;
    rect.width  = s_comp.pip_w;
    rect.height = s_comp.pip_h;

    /* Opaque copy of the SP rect (no alpha blend), matching the SDK PIP path. */
    vret = vg_lite_blit_rect(&dst, &src, &rect, &m, VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_POINT);
    if (vret != VG_LITE_SUCCESS)
    {
        LOGE("pip vg_lite_blit_rect failed=%d\n", vret);
        ret = AVDK_ERR_GENERIC;
    }
    vg_lite_finish();

    vg_lite_free_without_free_data(&dst);
    vg_lite_free_without_free_data(&src);

    (void)bk_gpu_ioctl(s_comp.gpu, BK_GPU_IOCTL_UNLOCK, NULL);
    return ret;
}

/* Supplemental PIP refresh for the downlink-idle case.
 *
 * The in-pipeline PIP blit (gpu_flex_data_frame_done) only fires when a downlink
 * main frame is composed, so a slow downlink drags the self-view down with it.
 * When the main picture is frozen (comp_downlink_idle) recompose the just-read
 * SP frame onto a copy of the on-screen main and flush it, keeping the self-view
 * at its own ~SP rate.
 *
 * Memory: reuses out_pool (no dedicated background buffer). The on-screen frame
 * stays pinned (in_use) while downlink is idle, so it is a safe memcpy source.
 * The caller owns sp_frame and releases it after this returns. */
static void comp_pip_idle_refresh(void *sp_frame)
{
    void *src;
    void *bg;

    src = s_comp.on_screen_frame;
    if (src == NULL || s_comp.out_pool_size == 0U)
    {
        return; /* no composed main picture yet */
    }

    bg = comp_out_alloc(s_comp.out_pool_size);
    if (bg == NULL)
    {
        return; /* no spare buffer this tick; skip (main picture keeps showing) */
    }

    /* Seed bg with the frozen main (carries a stale PIP region) via HPDMA; the
     * overlay blit then overwrites only the PIP rect with the fresh SP frame.
     * bk_hpdma_memcpy yields the CPU (semaphore) during the transfer and falls
     * back to a CPU copy internally if the DMA path fails, so bg is always
     * valid. */
    if (bk_hpdma_memcpy(bg, src, s_comp.out_pool_size) != BK_OK)
    {
        os_memcpy(bg, src, s_comp.out_pool_size);
    }

    if (comp_pip_blit_overlay(bg, sp_frame) == AVDK_ERR_OK)
    {
        comp_display_push(bg, false);
    }
    else
    {
        (void)comp_out_free(bg);
    }
}

static void comp_pip_task_entry(void *arg)
{
    uint32_t push_count = 0U;
    uint32_t fail_count = 0U;

    (void)arg;
    LOGI("pip task started, isp=%ux%u dst=(%u,%u) rotate=%u frame_size=%u\n",
         (unsigned)s_comp.pip_w, (unsigned)s_comp.pip_h,
         (unsigned)s_comp.pip_dst_x, (unsigned)s_comp.pip_dst_y,
         (unsigned)s_comp.pip_rotate, (unsigned)s_comp.pip_frame_size);

    while (s_comp.running != 0U && s_comp.pip_stop == 0U)
    {
        uint8_t *frame_buf;
        bk_gpu_blit_config_t blit;
        avdk_err_t ret;

        if (s_comp.gpu == NULL || s_comp.pip_frame_size == 0U)
        {
            rtos_delay_milliseconds(COMP_PIP_FAIL_DELAY_MS);
            continue;
        }

        if (push_count >= COMP_PIP_PRIME_COUNT && s_comp.pip_release_sem != NULL)
        {
            (void)rtos_get_semaphore(&s_comp.pip_release_sem, COMP_PIP_RELEASE_WAIT_MS);
        }

        frame_buf = (uint8_t *)comp_pip_alloc(s_comp.pip_frame_size);
        if (frame_buf == NULL)
        {
            rtos_delay_milliseconds(COMP_PIP_FAIL_DELAY_MS);
            continue;
        }

        /* Read a local self-view NV12 frame from the ISP SP channel (opened by
         * comp_pip_start via doorbell_isp_sp_open). */
        ret = doorbell_isp_sp_read_nv12(frame_buf, s_comp.pip_frame_size,
                                        COMP_PIP_READ_TIMEOUT_MS);
        if (ret != BK_OK)
        {
            comp_pip_free(frame_buf);
            fail_count++;
            /* Never let a dead SP source busy-spin and starve the decode path. */
            rtos_delay_milliseconds(COMP_PIP_FAIL_DELAY_MS);
            if (fail_count >= COMP_PIP_MAX_FAIL)
            {
                LOGW("pip: ISP SP read failed %u times (ret=%d), disabling PIP; "
                     "downlink main picture continues\n",
                     (unsigned)fail_count, (int)ret);
                break;
            }
            continue;
        }
        fail_count = 0U;

        if (comp_downlink_idle())
        {
            /* Downlink main picture is frozen: do a project-side PIP-only refresh
             * (own the VG-Lite blit and the SP frame), decoupled from the SDK
             * in-pipeline blit path so the self-view keeps its own ~SP rate. The
             * SP frame is fully consumed within this iteration. */
            comp_pip_idle_refresh(frame_buf);
            /* Release the SP frame and post pip_release_sem with the same
             * accounting the controller's free callback would use in the active
             * path, so the top-of-loop backpressure stays balanced. */
            comp_pip_blit_free_cb(frame_buf, NULL);
            push_count++;
        }
        else
        {
            /* Downlink active: hand the overlay to the GPU controller; the SDK
             * flex worker composites it during the next downlink frame_done. */
            os_memset(&blit, 0, sizeof(blit));
            blit.src_x = 0U;
            blit.src_y = 0U;
            blit.src_width = s_comp.pip_w;
            blit.src_height = s_comp.pip_h;
            blit.src_format = BK_PIXEL_FORMAT_NV12;
            blit.dst_x = s_comp.pip_dst_x;
            blit.dst_y = s_comp.pip_dst_y;
            blit.rotate_degree = s_comp.pip_rotate;
            blit.osd_slot = COMP_PIP_BLIT_SLOT;
            blit.args = NULL;
            blit.free = comp_pip_blit_free_cb;

            ret = bk_gpu_blit_set(s_comp.gpu, frame_buf, &blit);
            if (ret != AVDK_ERR_OK)
            {
                LOGW("pip blit_set failed=%d, drop frame\n", (int)ret);
                comp_pip_free(frame_buf);
                rtos_delay_milliseconds(5U);
            }
            else
            {
                push_count++;
            }
        }
    }

    LOGI("pip task exiting\n");
    if (s_comp.pip_done_sem != NULL)
    {
        (void)rtos_set_semaphore(&s_comp.pip_done_sem);
    }
    s_comp.pip_task = NULL;
    rtos_delete_thread(NULL);
}

static avdk_err_t comp_pip_start(void)
{
    bk_err_t bk_ret;
    avdk_err_t ret;

    s_comp.pip_frame_size = (uint32_t)s_comp.pip_w * (uint32_t)s_comp.pip_h * 3U / 2U;

    if (s_comp.pip_done_sem == NULL)
    {
        bk_ret = rtos_init_semaphore(&s_comp.pip_done_sem, 1);
        if (bk_ret != BK_OK)
        {
            return AVDK_ERR_GENERIC;
        }
    }
    if (s_comp.pip_release_sem == NULL)
    {
        bk_ret = rtos_init_semaphore(&s_comp.pip_release_sem, 1);
        if (bk_ret != BK_OK)
        {
            return AVDK_ERR_GENERIC;
        }
    }
    else
    {
        (void)rtos_get_semaphore(&s_comp.pip_release_sem, 0U);
    }

    ret = comp_pip_pool_init(s_comp.pip_frame_size);
    if (ret != AVDK_ERR_OK)
    {
        return ret;
    }

    s_comp.pip_stop = 0U;

    /* Open the ISP SP channel (sync at MP mid-frame) that feeds the self-view. */
    if (doorbell_isp_sp_open(s_comp.pip_w, s_comp.pip_h) != BK_OK)
    {
        LOGW("ISP SP open failed, PIP disabled\n");
        comp_pip_pool_deinit();
        return AVDK_ERR_GENERIC;
    }

    bk_ret = rtos_create_thread(&s_comp.pip_task, COMP_PIP_TASK_PRIORITY, "db_pip",
                                (beken_thread_function_t)comp_pip_task_entry,
                                COMP_PIP_TASK_STACK, NULL);
    if (bk_ret != BK_OK)
    {
        doorbell_isp_sp_close();
        comp_pip_pool_deinit();
        return AVDK_ERR_GENERIC;
    }
    return AVDK_ERR_OK;
}

static void comp_pip_stop(void)
{
    if (s_comp.pip_task != NULL && s_comp.pip_done_sem != NULL)
    {
        (void)rtos_get_semaphore(&s_comp.pip_done_sem, BEKEN_WAIT_FOREVER);
    }
    /* Release the ISP SP channel once the PIP task is guaranteed stopped. */
    doorbell_isp_sp_close();
    if (s_comp.gpu != NULL)
    {
        (void)bk_gpu_blit_clear_slot(s_comp.gpu, COMP_PIP_BLIT_SLOT);
    }
    comp_pip_pool_deinit();

    if (s_comp.pip_done_sem != NULL)
    {
        (void)rtos_deinit_semaphore(&s_comp.pip_done_sem);
        s_comp.pip_done_sem = NULL;
    }
    if (s_comp.pip_release_sem != NULL)
    {
        (void)rtos_deinit_semaphore(&s_comp.pip_release_sem);
        s_comp.pip_release_sem = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                        */
/* ------------------------------------------------------------------ */

uint32_t doorbell_compositor_gpu_pingpong_bytes(uint16_t dst_w, uint16_t dst_h)
{
    uint16_t out_w = (uint16_t)(((uint32_t)dst_w + 15U) & ~15U);

    (void)dst_h;
    /* compress=true: output_width_x_flexa_lines = output_width * flexa_lines */
    return (uint32_t)out_w * (uint32_t)COMP_GPU_FLEXA_LINES * 2U + 64U;
}

bk_err_t doorbell_compositor_start(const doorbell_compositor_config_t *cfg,
                                   uint8_t *flexa_ring,
                                   uint8_t flexa_buf_count)
{
    avdk_err_t ret;
    bk_gpu_ctlr_config_t gpu_cfg;
    gpu_board_config_t *board;
    uint16_t dst_w;
    uint16_t dst_h;
    uint8_t degree;
    uint32_t pool_buf_size;

    if (cfg == NULL || flexa_ring == NULL || flexa_buf_count == 0U ||
        cfg->main_width == 0U || cfg->main_height == 0U)
    {
        return BK_ERR_PARAM;
    }
    if (s_comp.gpu != NULL)
    {
        return BK_ERR_STATE;
    }

    board = app_gpu_board_config_get();
    if (board != NULL && board->flexa.dst_width != 0U && board->flexa.dst_height != 0U)
    {
        dst_w = board->flexa.dst_width;
        dst_h = board->flexa.dst_height;
        degree = board->flexa.degree;
    }
    else
    {
        /* hx8399c 1080x1920 portrait, pre-rotation 1920x1080 @ 90deg. */
        dst_w = 1920U;
        dst_h = 1080U;
        degree = 90U;
    }

    /* Mirror the GPU controller's output geometry (bk_gpu_ctlr_default.c:
     * output_width = compress ? align16(dst_width) : dst_width, output_height =
     * align16(dst_height)); the compositor always runs compressed ARGB. Used to
     * describe the DPU-facing dst buffer to VG-Lite in the PIP-only blit. */
    s_comp.out_disp_w = (uint16_t)(((uint32_t)dst_w + 15U) & ~15U);
    s_comp.out_disp_h = (uint16_t)(((uint32_t)dst_h + 15U) & ~15U);
    s_comp.out_rotate = degree;

    if (s_comp.display_release_sem == NULL)
    {
        if (rtos_init_semaphore(&s_comp.display_release_sem, 1) != BK_OK)
        {
            return BK_FAIL;
        }
    }
    else
    {
        (void)rtos_get_semaphore(&s_comp.display_release_sem, 0U);
    }
    s_comp.display_pushed = 0U;
    s_comp.on_screen_frame = NULL;
    s_comp.last_downlink_flush_ms = (uint32_t)rtos_get_time();

    /* The GPU compressed-ARGB output allocates height rounded up to 16 lines
     * (mirrors h264d_gpu_display_gpu_blit.c OUTPUT_HEIGHT). The pool buffer size
     * MUST use the same aligned height, otherwise comp_out_alloc's size match
     * never hits, the pre-allocated pool is dead weight, and every GPU frame is
     * malloc'd fresh from MEM_SLAB_HEAP_UNCODED - which exhausts that heap once
     * the PIP self-view pool is also live and freezes the whole compositor. */
    pool_buf_size = (uint32_t)bk_pixel_size_get(BK_PIXEL_FORMAT_ARGB8888) *
                    ((uint32_t)dst_w / 4U) * (((uint32_t)dst_h + 15U) & ~15U);
    s_comp.out_pool_size = pool_buf_size;
    if (!cfg->defer_out_pool)
    {
        ret = comp_out_pool_init(pool_buf_size);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("out pool init failed=%d size=%u\n", (int)ret, (unsigned)pool_buf_size);
            goto fail_sem;
        }
    }

    os_memset(&gpu_cfg, 0, sizeof(gpu_cfg));
    gpu_cfg.rotate_degree = degree;
    gpu_cfg.src_width = cfg->main_width;
    gpu_cfg.src_height = cfg->main_height;
    gpu_cfg.dst_width = dst_w;
    gpu_cfg.dst_height = dst_h;
    gpu_cfg.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_cfg.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_cfg.scale = true;
    gpu_cfg.compress = true;
    gpu_cfg.src_buffer = flexa_ring;
    gpu_cfg.flexa = true;
    gpu_cfg.flexa_lines = COMP_GPU_FLEXA_LINES;
    gpu_cfg.flexa_buff_cnt = flexa_buf_count;
    gpu_cfg.frame_malloc = comp_out_alloc;
    gpu_cfg.frame_free = comp_out_free;
    gpu_cfg.flexa_line_done = NULL;
    gpu_cfg.flexa_line_done_args = NULL;
    gpu_cfg.frame_done = comp_frame_done;
    gpu_cfg.frame_done_args = NULL;

    ret = bk_gpu_ctlr_new(&s_comp.gpu, &gpu_cfg);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("gpu ctlr_new failed=%d\n", (int)ret);
        goto fail_pool;
    }
    ret = bk_gpu_init(s_comp.gpu);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("gpu init failed=%d\n", (int)ret);
        goto fail_gpu;
    }

    s_comp.main_width = cfg->main_width;
    s_comp.main_height = cfg->main_height;
    s_comp.pip_enable = cfg->pip_enable;
    if (cfg->pip_enable)
    {
        s_comp.pip_w = cfg->pip_width;
        s_comp.pip_h = cfg->pip_height;
        s_comp.pip_dst_x = cfg->pip_dst_x;
        s_comp.pip_dst_y = cfg->pip_dst_y;
        s_comp.pip_rotate = cfg->pip_rotate;
    }

    if (cfg->defer_gpu_open)
    {
        s_comp.gpu_open_deferred = 1U;
        LOGI("compositor gpu init done (defer open): main=%ux%u dst=%ux%u deg=%u pip=%u\n",
             (unsigned)cfg->main_width, (unsigned)cfg->main_height,
             (unsigned)dst_w, (unsigned)dst_h, (unsigned)degree,
             (unsigned)s_comp.pip_enable);
        return BK_OK;
    }

    doorbell_downlink_release_pingpong_hold();

    ret = bk_gpu_open(s_comp.gpu);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("gpu open failed=%d hsram_free=%u\n", (int)ret,
             (unsigned)rtos_get_hsram_free_heap_size());
        goto fail_gpu;
    }

    s_comp.running = 1U;

    if (cfg->pip_enable)
    {
        if (comp_pip_start() != AVDK_ERR_OK)
        {
            LOGW("pip start failed, main picture continues without PIP\n");
            s_comp.pip_enable = false;
        }
    }

    LOGI("compositor start: main=%ux%u dst=%ux%u deg=%u pip=%u\n",
         (unsigned)cfg->main_width, (unsigned)cfg->main_height,
         (unsigned)dst_w, (unsigned)dst_h, (unsigned)degree,
         (unsigned)s_comp.pip_enable);
    comp_notify_gpu_event(true);
    return BK_OK;

fail_gpu:
    if (s_comp.gpu != NULL)
    {
        if (s_comp.gpu_open_deferred == 0U)
        {
            (void)bk_gpu_close(s_comp.gpu);
        }
        (void)bk_gpu_deinit(s_comp.gpu);
        (void)bk_gpu_delete(s_comp.gpu);
        s_comp.gpu = NULL;
    }
fail_pool:
    comp_out_pool_deinit();
fail_sem:
    if (s_comp.display_release_sem != NULL)
    {
        (void)rtos_deinit_semaphore(&s_comp.display_release_sem);
        s_comp.display_release_sem = NULL;
    }
    s_comp.gpu_open_deferred = 0U;
    s_comp.running = 0U;
    return BK_FAIL;
}

bk_err_t doorbell_compositor_gpu_open(void)
{
    avdk_err_t ret;

    if (s_comp.gpu == NULL || s_comp.gpu_open_deferred == 0U)
    {
        return BK_ERR_STATE;
    }

    doorbell_downlink_release_pingpong_hold();

    ret = bk_gpu_open(s_comp.gpu);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("gpu open failed=%d hsram_free=%u\n", (int)ret,
             (unsigned)rtos_get_hsram_free_heap_size());
        return BK_FAIL;
    }

    s_comp.gpu_open_deferred = 0U;
    s_comp.running = 1U;

    if (s_comp.pip_enable)
    {
        if (comp_pip_start() != AVDK_ERR_OK)
        {
            LOGW("pip start failed, main picture continues without PIP\n");
            s_comp.pip_enable = false;
        }
    }

    LOGI("compositor gpu open done: main=%ux%u pip=%u\n",
         (unsigned)s_comp.main_width, (unsigned)s_comp.main_height,
         (unsigned)s_comp.pip_enable);
    comp_notify_gpu_event(true);
    return BK_OK;
}

bk_err_t doorbell_compositor_out_pool_init(void)
{
    avdk_err_t ret;

    if (s_comp.gpu == NULL)
    {
        return BK_ERR_STATE;
    }
    if (s_comp.out_pool_count != 0U)
    {
        return BK_OK;
    }
    if (s_comp.out_pool_size == 0U)
    {
        return BK_ERR_PARAM;
    }

    ret = comp_out_pool_init(s_comp.out_pool_size);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("deferred out pool init failed=%d size=%u\n",
             (int)ret, (unsigned)s_comp.out_pool_size);
        return BK_FAIL;
    }
    LOGI("compositor out pool ready: %u x %u bytes\n",
         (unsigned)s_comp.out_pool_count, (unsigned)s_comp.out_pool_size);
    return BK_OK;
}

bk_err_t doorbell_compositor_pip_enable(const doorbell_compositor_config_t *cfg)
{
    if (cfg == NULL)
    {
        return BK_ERR_PARAM;
    }
    if (s_comp.gpu == NULL || s_comp.running == 0U)
    {
        return BK_ERR_STATE;
    }
    if (s_comp.pip_enable)
    {
        return BK_OK; /* already showing the self-view */
    }
    if (!cfg->pip_enable)
    {
        return BK_ERR_STATE;
    }

    s_comp.pip_w = cfg->pip_width;
    s_comp.pip_h = cfg->pip_height;
    s_comp.pip_dst_x = cfg->pip_dst_x;
    s_comp.pip_dst_y = cfg->pip_dst_y;
    s_comp.pip_rotate = cfg->pip_rotate;

    if (comp_pip_start() != AVDK_ERR_OK)
    {
        LOGW("runtime pip start failed, main picture continues without PIP\n");
        return BK_FAIL;
    }
    s_comp.pip_enable = true;
    LOGI("runtime PIP enabled: %ux%u dst=(%u,%u) rot=%u\n",
         (unsigned)s_comp.pip_w, (unsigned)s_comp.pip_h,
         (unsigned)s_comp.pip_dst_x, (unsigned)s_comp.pip_dst_y,
         (unsigned)s_comp.pip_rotate);
    return BK_OK;
}

bk_err_t doorbell_compositor_pip_disable(void)
{
    if (s_comp.gpu == NULL || s_comp.running == 0U)
    {
        return BK_ERR_STATE;
    }
    if (!s_comp.pip_enable)
    {
        return BK_OK; /* self-view already off */
    }

    /* Signal the PIP task to exit (the main compositor keeps running), then reuse
     * comp_pip_stop which joins the task, closes the ISP SP channel and clears the
     * GPU blit overlay so the small self-view window disappears on the next
     * composed frame instead of freezing on the last SP frame. */
    s_comp.pip_stop = 1U;
    comp_pip_stop();
    s_comp.pip_enable = false;
    LOGI("runtime PIP disabled (uplink/camera off)\n");
    return BK_OK;
}

void doorbell_compositor_stop(void)
{
    if (s_comp.gpu == NULL)
    {
        return;
    }

    s_comp.running = 0U;
    s_comp.gpu_open_deferred = 0U;

    if (s_comp.pip_enable)
    {
        comp_pip_stop();
        s_comp.pip_enable = false;
    }

    /* Product LCD OSD must drop bk_draw_osd before the GPU is closed. */
    comp_notify_gpu_event(false);

#if CONFIG_DOOR_ACCESS_CALL_FLOW
    /* Tear the OSD controller down while the GPU is still alive; bk_draw_osd_delete
     * releases the overlay layers it leased. */
    if (s_comp.osd != NULL)
    {
        (void)bk_draw_osd_delete(s_comp.osd);
        s_comp.osd = NULL;
    }
    s_comp.osd_elem_count = 0U;
#endif

    if (s_comp.gpu != NULL)
    {
        (void)bk_gpu_close(s_comp.gpu);
        (void)bk_gpu_deinit(s_comp.gpu);
        (void)bk_gpu_delete(s_comp.gpu);
        s_comp.gpu = NULL;
    }

    /* The DPU still holds the last displayed frame together with its
     * comp_dpu_release callback (it releases a frame only when the NEXT flush is
     * committed, which now comes from the next display owner, e.g. a rebuilt
     * LVGL). That late release is made harmless by the s_comp.running == 0 guard
     * in comp_out_free(): it skips the raw bk_frame_buffer_free() so the buffer
     * this pool freed below (and the slab may have recycled to LVGL) is not
     * double-freed -> avoids the "frame buffer overflow" assert. */
    comp_out_pool_deinit();

    if (s_comp.display_release_sem != NULL)
    {
        (void)rtos_deinit_semaphore(&s_comp.display_release_sem);
        s_comp.display_release_sem = NULL;
    }
    s_comp.display_pushed = 0U;
    s_comp.on_screen_frame = NULL;
    LOGI("compositor stopped\n");
}

bk_gpu_ctlr_handle_t doorbell_compositor_gpu_handle_get(void)
{
    return s_comp.gpu;
}

void doorbell_compositor_set_gpu_event_cb(doorbell_compositor_gpu_event_cb_t cb)
{
    s_comp.gpu_event_cb = cb;
}

bool doorbell_compositor_is_running(void)
{
    return (s_comp.gpu != NULL && s_comp.running != 0U);
}

#if CONFIG_DOOR_ACCESS_CALL_FLOW
/* ------------------------------------------------------------------ */
/* OSD sprite (hang-up button) via public bk_draw_osd — indoor-display */
/* ------------------------------------------------------------------ */

static bk_err_t comp_osd_ensure(uint16_t rotate)
{
    osd_ctlr_config_t cfg;
    avdk_err_t ret;

    if (s_comp.osd != NULL)
    {
        return BK_OK;
    }

    os_memset(&cfg, 0, sizeof(cfg));
    cfg.gpu = s_comp.gpu;
    /* GPU flexa dst (out_disp_w/h) is pre-rotation, e.g. 1280x720 @ 90deg.
     * The composed buffer the overlay blit targets is the post-rotation panel
     * (720x1280). doorbell_call already places the hang-up sprite in that
     * portrait space, matching draw_osd_example MIPI (panel after rotate90,
     * osd_rotate_degree=0). */
    if (s_comp.out_rotate == 90U || s_comp.out_rotate == 270U)
    {
        cfg.panel_w = s_comp.out_disp_h;
        cfg.panel_h = s_comp.out_disp_w;
    }
    else
    {
        cfg.panel_w = s_comp.out_disp_w;
        cfg.panel_h = s_comp.out_disp_h;
    }
    cfg.osd_rotate_degree = rotate;
    /* Hang-up asset is packed (A<<24)|(R<<16)|(G<<8)|B, matching ARGB8888 overlay. */
    cfg.src_format = BK_PIXEL_FORMAT_ARGB8888;
    /* Asset table must already be filled: bk_draw_osd_new captures its size
     * here and the elements are then added to the dynamic list by name. */
    cfg.blend_assets = s_comp.osd_assets;
    cfg.blend_info = NULL;

    ret = bk_draw_osd_new(&s_comp.osd, &cfg);
    if (ret != AVDK_ERR_OK)
    {
        s_comp.osd = NULL;
        LOGE("bk_draw_osd_new failed=%d\n", (int)ret);
        return BK_FAIL;
    }
    return BK_OK;
}

/* Copy one sprite's geometry and pixel pointer into its element descriptor.
 * Returns true when the element now differs from what is on screen. */
static bool comp_osd_blend_update(bk_blend_t *blend, const doorbell_osd_sprite_t *sprite)
{
    bool changed = blend->image.data != (const uint8_t *)sprite->argb8888 ||
                   blend->width != (uint32_t)sprite->width ||
                   blend->height != (uint32_t)sprite->height ||
                   blend->xpos != sprite->dst_x ||
                   blend->ypos != sprite->dst_y;

    blend->blend_type = BLEND_TYPE_IMAGE;
    blend->width = sprite->width;
    blend->height = sprite->height;
    blend->xpos = sprite->dst_x;
    blend->ypos = sprite->dst_y;
    blend->image.format = ARGB8888;
    blend->image.data_len = (uint32_t)sprite->width * (uint32_t)sprite->height * 4U;
    blend->image.data = (const uint8_t *)sprite->argb8888;
    return changed;
}

static bool comp_osd_table_matches(const doorbell_osd_sprite_t *sprites, uint8_t count)
{
    uint8_t i;

    if (count != s_comp.osd_elem_count)
    {
        return false;
    }
    for (i = 0U; i < count; i++)
    {
        if (os_strcmp(s_comp.osd_assets[i].name, sprites[i].name) != 0)
        {
            return false;
        }
    }
    return true;
}

static void comp_osd_table_build(const doorbell_osd_sprite_t *sprites, uint8_t count)
{
    uint8_t i;

    /* bk_blend_t has a const name[] member, so the descriptors are zeroed and
     * filled field by field rather than assigned. name is only used as fallback
     * text for FONT elements and stays empty for these image sprites. */
    os_memset(s_comp.osd_blend, 0, sizeof(s_comp.osd_blend));
    os_memset(s_comp.osd_assets, 0, sizeof(s_comp.osd_assets));

    for (i = 0U; i < count; i++)
    {
        (void)comp_osd_blend_update(&s_comp.osd_blend[i], &sprites[i]);
        os_strncpy(s_comp.osd_assets[i].name, sprites[i].name, MAX_BLEND_NAME_LEN - 1U);
        s_comp.osd_assets[i].addr = &s_comp.osd_blend[i];
    }
    /* osd_assets[count].addr stays NULL: asset table terminator. */
    s_comp.osd_elem_count = count;
}

bk_err_t doorbell_compositor_osd_show_n(const doorbell_osd_sprite_t *sprites, uint8_t count,
                                        uint16_t rotate)
{
    avdk_err_t ret;
    uint8_t i;

    if (sprites == NULL || count == 0U)
    {
        return BK_ERR_PARAM;
    }
    if (count > DOORBELL_OSD_SPRITE_MAX)
    {
        count = (uint8_t)DOORBELL_OSD_SPRITE_MAX;
    }
    for (i = 0U; i < count; i++)
    {
        if (sprites[i].argb8888 == NULL || sprites[i].width == 0U ||
            sprites[i].height == 0U || sprites[i].name == NULL ||
            sprites[i].name[0] == '\0')
        {
            return BK_ERR_PARAM;
        }
    }
    if (s_comp.gpu == NULL || s_comp.running == 0U)
    {
        return BK_ERR_STATE;
    }

    /* bk_draw_osd_new captures the asset table size, so a different element set
     * needs a fresh instance. */
    if (s_comp.osd != NULL && !comp_osd_table_matches(sprites, count))
    {
        (void)bk_draw_osd_delete(s_comp.osd);
        s_comp.osd = NULL;
    }

    if (s_comp.osd == NULL)
    {
        comp_osd_table_build(sprites, count);
        if (comp_osd_ensure(rotate) != BK_OK)
        {
            s_comp.osd_elem_count = 0U;
            return BK_FAIL;
        }
        for (i = 0U; i < count; i++)
        {
            (void)bk_draw_osd_add_or_update(s_comp.osd, s_comp.osd_assets[i].name, NULL);
        }
    }
    else
    {
        /* Mark only what moved or was repainted. Everything else keeps the
         * overlay layer it is already composited from. */
        for (i = 0U; i < count; i++)
        {
            if (comp_osd_blend_update(&s_comp.osd_blend[i], &sprites[i]) || sprites[i].dirty)
            {
                (void)bk_draw_osd_add_or_update(s_comp.osd, s_comp.osd_assets[i].name, NULL);
            }
        }
    }

    /* Dynamic list (NULL) instead of an explicit array: an explicit list forces
     * a full repaint, which drops every layer's committed blit first. */
    ret = bk_draw_osd_array(s_comp.osd, NULL);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("bk_draw_osd_array failed=%d count=%u\n", (int)ret, (unsigned)count);
        return BK_FAIL;
    }
    return BK_OK;
}

bk_err_t doorbell_compositor_osd_show(const char *name, const void *argb8888,
                                      uint16_t width, uint16_t height,
                                      uint16_t dst_x, uint16_t dst_y, uint16_t rotate)
{
    doorbell_osd_sprite_t sprite;

    os_memset(&sprite, 0, sizeof(sprite));
    sprite.name = name;
    sprite.argb8888 = argb8888;
    sprite.width = width;
    sprite.height = height;
    sprite.dst_x = dst_x;
    sprite.dst_y = dst_y;
    sprite.dirty = true;
    return doorbell_compositor_osd_show_n(&sprite, 1U, rotate);
}

bk_err_t doorbell_compositor_osd_hide(void)
{
    if (s_comp.osd != NULL)
    {
        (void)bk_draw_osd_clear(s_comp.osd);
    }
    return BK_OK;
}
#endif /* CONFIG_DOOR_ACCESS_CALL_FLOW */
