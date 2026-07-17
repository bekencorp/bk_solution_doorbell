#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <common/avdk_pixel_types.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_gpu_ctlr.h>
#include <components/bk_gpu.h>
#include <components/bk_isp_camera.h>
#include <driver/isp_base.h>

#include "app_display.h"
#include "app_gpu.h"
#include "app_camera.h"
#include "doorbell_devices.h"
#include "doorbell_isp_sp.h"
#include "doorbell_display_compositor.h"

#define TAG "db-comp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define COMP_GPU_FLEXA_LINES     16U
#define COMP_FRAME_POOL_COUNT    2U
#define COMP_DISPLAY_PRIME_COUNT 2U
#define COMP_DISPLAY_WAIT_MS     100U

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

typedef struct
{
    void *buf;
    uint8_t in_use;
} comp_pool_entry_t;

typedef struct
{
    bk_gpu_ctlr_handle_t gpu;
    volatile uint8_t running;

    /* GPU output (ARGB) frame pool -> DPU. */
    comp_pool_entry_t out_pool[COMP_FRAME_POOL_COUNT];
    uint32_t out_pool_size;
    uint32_t out_pool_count;
    beken_semaphore_t display_release_sem;
    volatile uint32_t display_pushed;

    /* PIP overlay (ISP SP self-view). */
    bool pip_enable;
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
} comp_ctx_t;

static comp_ctx_t s_comp = {0};

/* ------------------------------------------------------------------ */
/* GPU output frame pool                                              */
/* ------------------------------------------------------------------ */

static void *comp_out_alloc(uint32_t size)
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

    if (ptr == NULL)
    {
        ptr = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);
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
        bk_frame_buffer_free(ptr);
    }
    return AVDK_ERR_OK;
}

static avdk_err_t comp_out_pool_init(uint32_t buf_size)
{
    uint32_t i;

    if (buf_size == 0U)
    {
        return AVDK_ERR_INVAL;
    }

    for (i = 0; i < COMP_FRAME_POOL_COUNT; i++)
    {
        s_comp.out_pool[i].buf = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, buf_size);
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
    return AVDK_ERR_OK;
}

static void comp_out_pool_deinit(void)
{
    uint32_t i;

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

static void comp_frame_done(void *frame, uint32_t frame_size, void *args)
{
    int ret;

    (void)frame_size;
    (void)args;

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

static void comp_pip_task_entry(void *arg)
{
    uint32_t push_count = 0U;
    uint32_t fail_count = 0U;

    (void)arg;
    LOGI("pip task started, isp=%ux%u dst=(%u,%u) rotate=%u frame_size=%u\n",
         (unsigned)s_comp.pip_w, (unsigned)s_comp.pip_h,
         (unsigned)s_comp.pip_dst_x, (unsigned)s_comp.pip_dst_y,
         (unsigned)s_comp.pip_rotate, (unsigned)s_comp.pip_frame_size);

    while (s_comp.running != 0U)
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

        os_memset(&blit, 0, sizeof(blit));
        blit.src_x = 0U;
        blit.src_y = 0U;
        blit.src_width = s_comp.pip_w;
        blit.src_height = s_comp.pip_h;
        blit.src_format = BK_PIXEL_FORMAT_NV12;
        blit.dst_x = s_comp.pip_dst_x;
        blit.dst_y = s_comp.pip_dst_y;
        blit.rotate_degree = s_comp.pip_rotate;
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
        (void)bk_gpu_blit_clear(s_comp.gpu);
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

    pool_buf_size = (uint32_t)bk_pixel_size_get(BK_PIXEL_FORMAT_ARGB8888) *
                    ((uint32_t)dst_w / 4U) * (uint32_t)dst_h;
    ret = comp_out_pool_init(pool_buf_size);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("out pool init failed=%d size=%u\n", (int)ret, (unsigned)pool_buf_size);
        goto fail_sem;
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
    ret = bk_gpu_open(s_comp.gpu);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("gpu open failed=%d\n", (int)ret);
        goto fail_gpu;
    }

    s_comp.running = 1U;

    s_comp.pip_enable = cfg->pip_enable;
    if (cfg->pip_enable)
    {
        s_comp.pip_w = cfg->pip_width;
        s_comp.pip_h = cfg->pip_height;
        s_comp.pip_dst_x = cfg->pip_dst_x;
        s_comp.pip_dst_y = cfg->pip_dst_y;
        s_comp.pip_rotate = cfg->pip_rotate;

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
    return BK_OK;

fail_gpu:
    if (s_comp.gpu != NULL)
    {
        (void)bk_gpu_close(s_comp.gpu);
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
    s_comp.running = 0U;
    return BK_FAIL;
}

void doorbell_compositor_stop(void)
{
    if (s_comp.gpu == NULL && s_comp.running == 0U)
    {
        return;
    }

    s_comp.running = 0U;

    if (s_comp.pip_enable)
    {
        comp_pip_stop();
        s_comp.pip_enable = false;
    }

    if (s_comp.gpu != NULL)
    {
        (void)bk_gpu_close(s_comp.gpu);
        (void)bk_gpu_deinit(s_comp.gpu);
        (void)bk_gpu_delete(s_comp.gpu);
        s_comp.gpu = NULL;
    }

    comp_out_pool_deinit();

    if (s_comp.display_release_sem != NULL)
    {
        (void)rtos_deinit_semaphore(&s_comp.display_release_sem);
        s_comp.display_release_sem = NULL;
    }
    s_comp.display_pushed = 0U;
    LOGI("compositor stopped\n");
}

bk_gpu_ctlr_handle_t doorbell_compositor_gpu_handle_get(void)
{
    return s_comp.gpu;
}

bool doorbell_compositor_is_running(void)
{
    return (s_comp.gpu != NULL && s_comp.running != 0U);
}
