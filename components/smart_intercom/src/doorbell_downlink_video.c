#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <common/avdk_pixel_types.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_hardware_ram.h>
#include <components/bk_decode/bk_h264_decode_ctlr.h>
#include <components/bk_flexa_bond.h>

#include "app_gpu.h"
#include "doorbell_devices.h"
#include "doorbell_devices_intercom.h"
#include "doorbell_downlink_img_manager.h"
#include "doorbell_display_compositor.h"
#include "doorbell_downlink_video.h"
#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
#include "network_transfer.h"
#endif

#define TAG "db-dl-vid"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define DL_SEG_HEIGHT_MB     1U
/* h264d -> GPU FLEXA ring depth (in 16-line macroblock-row segments).
 *
 * ring size = width * (16 * DL_SEG_HEIGHT_MB * DL_SEG_NUM) * 3/2.
 * The ring is hsram_malloc'd BEFORE the compositor GPU's 128KB (0x20000)
 * contiguous buffer (also hsram), and the 720P uplink (ISP MP + H264 encode)
 * runs concurrently, so HSRAM is tight. At the reduced 640x360 uplink/downlink
 * size a 4-segment ring is only 640*64*3/2 = 60KB, leaving a contiguous 128KB
 * hole for the GPU. 4 segments matches the reference h264d_gpu_display and is
 * deep enough that the decode->GPU handoff does not starve under the concurrent
 * compositor scale/rotate + PIP overlay blit (2 segments stalled immediately
 * once the PIP blit was added). */
/* Seams are decode->GPU FLEXA ring-wrap artifacts: the ring holds DL_SEG_NUM
 * 16-line rows, so a frame taller than DL_SEG_NUM*16 wraps mid-picture and the
 * GPU reads partially-overwritten slots (period = DL_SEG_NUM MB-rows: 4->4
 * lines, 6->3 lines; pushing to 15 while the frame is 18 rows tipped the main
 * FLEXA read into all-zero/green output). A FULL-FRAME ring (DL_SEG_NUM ==
 * frame height / 16) has no intra-frame wrap and is deterministically seam-free.
 * At 512x288 the frame is 18 rows; a full-frame ring (18 seg, ~221KB) is
 * seam-free but does NOT fit HSRAM (GPU 128KB composite + uplink encode + PIP
 * are concurrent), so a shallow 4-segment ring (~48KB) is used here. That may
 * reintroduce mid-picture ring-wrap seams; the seam-free HSRAM-safe sweet spot
 * was 256x144 (9 seg = full frame, ~55KB). The decoder also rejects
 * segment_number > frame blocks. */
/* At the 256x144 downlink size the frame is 9 x 16-line rows, so DL_SEG_NUM=9
 * makes the FLEXA ring a FULL frame (~55KB): no mid-picture wrap, the h264d->GPU
 * bond never reads a partially-overwritten slot, and the hardware decoder stops
 * raising VCDEC_DEC_INT_ERROR. (512x288 = 18 rows would need 18 seg / ~221KB
 * which does not fit HSRAM; that is why the larger size was unstable.) */
#define DL_SEG_NUM           9U
#define DL_SLOT_COUNT        3U
#define DL_DECODE_TIMEOUT_MS 1000U
#define DL_POP_TIMEOUT_MS    100U

#define DL_TASK_PRIORITY     3U
#define DL_TASK_STACK        (1024U * 16U)

/* Default local self-view (PIP) geometry. */
/* PIP self-view window. Enlarged to 640x360 to match the PIP small-window size
 * of h264d_gpu_display_example. MUST match camera_board.isp.sp_width/height in
 * ap_main.c. SP is non-flexa, so only WIDTH needs 16-alignment for GPU/DMA
 * stride (640/16=40) and both dims must be even for NV12 (360 is even).
 * At 90/270 blit rotation a 640x360 source occupies a 360x640 footprint in the
 * 1080x1920 display space (688,32 right-anchored), which fits on screen. */
#define DL_PIP_WIDTH         640U
#define DL_PIP_HEIGHT        360U
#define DL_PIP_MARGIN        32U

typedef struct
{
    volatile uint8_t running;
    volatile uint8_t stop_request;

    doorbell_downlink_h264_config_t cfg;

    uint8_t *pp_buf;         /* FLEXA ring (64-byte aligned)         */
    uint32_t pp_size;
    bk_h264_decode_ctlr_handle_t decoder;
    void *bond;

    beken_thread_t task;
    beken_semaphore_t done_sem;
} dl_video_ctx_t;

static dl_video_ctx_t s_dl = {0};

/* FLEXA ring is consumed by GPU/DMA directly -> 64-byte alignment. */
static void *dl_hsram_aligned_malloc(uint32_t alignment, uint32_t size)
{
    void *raw;
    uint32_t total;
    uintptr_t start;
    uintptr_t aligned;

    if (alignment < (uint32_t)sizeof(void *))
    {
        alignment = (uint32_t)sizeof(void *);
    }
    if ((alignment & (alignment - 1U)) != 0U)
    {
        return NULL;
    }

    total = size + alignment - 1U + (uint32_t)sizeof(void *);
    raw = hsram_malloc(total);
    if (raw == NULL)
    {
        return NULL;
    }

    start = (uintptr_t)raw + sizeof(void *);
    aligned = (start + (alignment - 1U)) & ~((uintptr_t)alignment - 1U);
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

static void dl_hsram_aligned_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    os_free(((void **)ptr)[-1]);
}

static void dl_decode_task_entry(void *arg)
{
    uint32_t decoded = 0U;

    (void)arg;
    LOGI("decode task started, %ux%u\n", (unsigned)s_dl.cfg.width, (unsigned)s_dl.cfg.height);

    while (s_dl.stop_request == 0U)
    {
        downlink_frame_t *frame;
        bk_h264_decode_input_t input = {0};
        bk_h264_decode_info_t info = {0};
        avdk_err_t ret;

        frame = doorbell_downlink_ready_pop(DL_POP_TIMEOUT_MS);
        if (frame == NULL)
        {
            continue;
        }

        input.stream = frame->data;
        input.stream_len = frame->size;
        input.out_buffer = s_dl.pp_buf;
        input.out_buffer_size = s_dl.pp_size;

        ret = bk_h264_decode_frame(s_dl.decoder, &input);
        if (ret == AVDK_ERR_OK)
        {
            (void)bk_h264_decode_get_info(s_dl.decoder, &info);
            decoded++;
        }
        else
        {
            /* Recoverable: next IDR resets reference state. */
            LOGW("decode failed ret=%d (decoded=%u)\n", (int)ret, (unsigned)decoded);
        }

        doorbell_downlink_free_push(frame);
    }

    LOGI("decode task exiting, decoded=%u\n", (unsigned)decoded);
    if (s_dl.done_sem != NULL)
    {
        (void)rtos_set_semaphore(&s_dl.done_sem);
    }
    s_dl.task = NULL;
    rtos_delete_thread(NULL);
}

static void dl_teardown(void)
{
    if (s_dl.bond != NULL)
    {
        bk_flexa_h264d_gpu_bond_stop(s_dl.bond);
        s_dl.bond = NULL;
    }

    doorbell_compositor_stop();

    if (s_dl.decoder != NULL)
    {
        (void)bk_h264_decode_close(s_dl.decoder);
        (void)bk_h264_decode_deinit(s_dl.decoder);
        (void)bk_h264_decode_delete(s_dl.decoder);
        s_dl.decoder = NULL;
    }

    doorbell_downlink_img_manager_deinit();

    if (s_dl.pp_buf != NULL)
    {
        dl_hsram_aligned_free(s_dl.pp_buf);
        s_dl.pp_buf = NULL;
    }
    s_dl.pp_size = 0U;

    /* Restore the single-view preview GPU bond now that the downlink
     * compositor has released the GPU and its HSRAM ring. */
    (void)doorbell_devices_preview_gpu_attach();
}

bk_err_t doorbell_downlink_video_stop(void)
{
    if (s_dl.running == 0U)
    {
        return BK_OK;
    }

    s_dl.stop_request = 1U;
    if (s_dl.task != NULL && s_dl.done_sem != NULL)
    {
        (void)rtos_get_semaphore(&s_dl.done_sem, BEKEN_WAIT_FOREVER);
    }

    dl_teardown();

    if (s_dl.done_sem != NULL)
    {
        (void)rtos_deinit_semaphore(&s_dl.done_sem);
        s_dl.done_sem = NULL;
    }

    s_dl.running = 0U;
    s_dl.stop_request = 0U;
    LOGI("downlink video stopped\n");
    return BK_OK;
}

#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
/*
 * Zero-copy downlink: the bk_network_transfer unfragment layer reassembles each
 * H.264 access unit directly into an img_manager slot, so no os_memcpy is needed
 * on the network path (see 下行视频零拷贝优化方案.md).
 *
 * malloc_cb -> hand out a free slot as a frame_buffer (reassembly writes here)
 * send_cb   -> a full AU is in the slot; publish it to the decode ready FIFO
 * free_cb   -> return the slot to the free stack
 *
 * The callbacks are safe to call after the pool is torn down: alloc returns NULL
 * (transfer layer then drops the frame) and commit/release become no-ops. Note:
 * dl_teardown() releases the pool; callers must ensure the video channel has
 * stopped delivering before/soon after teardown to bound any in-flight window.
 */
static frame_buffer_t *dl_unfrag_malloc_cb(uint32_t size)
{
    return doorbell_downlink_slot_fb_alloc(size);
}

static bk_err_t dl_unfrag_send_cb(frame_buffer_t *fb)
{
    return doorbell_downlink_slot_fb_commit(fb);
}

static bk_err_t dl_unfrag_free_cb(frame_buffer_t *fb)
{
    return doorbell_downlink_slot_fb_release(fb);
}

static void dl_zerocopy_register(void)
{
    bk_err_t r1, r2, r3;

    /* Must be registered as a set; registration fails (BK_FAIL) if the active
     * transfer service has not started the video unfragment path, in which case
     * the network frame arrives via doorbell_downlink_video_recv() and is copied
     * into a slot as before (graceful fallback). */
    r1 = ntwk_trans_register_unfragment_malloc_cb(NTWK_TRANS_CHAN_VIDEO, dl_unfrag_malloc_cb);
    r2 = ntwk_trans_register_unfragment_send_cb(NTWK_TRANS_CHAN_VIDEO, dl_unfrag_send_cb);
    r3 = ntwk_trans_register_unfragment_free_cb(NTWK_TRANS_CHAN_VIDEO, dl_unfrag_free_cb);

    if (r1 == BK_OK && r2 == BK_OK && r3 == BK_OK)
    {
        LOGI("downlink zero-copy enabled (unfragment -> slot)\n");
    }
    else
    {
        LOGW("downlink zero-copy unavailable (r=%d,%d,%d), using copy path\n",
             (int)r1, (int)r2, (int)r3);
    }
}
#endif /* CONFIG_SMART_INTERCOM_DL_ZEROCOPY */

static bk_err_t dl_start(const doorbell_downlink_h264_config_t *cfg)
{
    doorbell_compositor_config_t comp_cfg = {0};
    bk_h264_decode_flexa_config_t dec_cfg = DEFAULT_H264_DECODE_FLEXA_CONFIG;
    gpu_board_config_t *board;
    uint16_t dst_w = 1920U;
    uint16_t dst_h = 1080U;
    uint32_t slot_capacity;
    bk_err_t bk_ret;
    avdk_err_t ret;

    s_dl.cfg = *cfg;

    /* Enter intercom mode: hand the GPU (and its HSRAM FLEXA ring) over from the
     * single-view preview to the downlink compositor. The uplink ISP MP -> H264
     * encode bond keeps running; ISP SP feeds the PIP self-view. */
    (void)doorbell_devices_preview_gpu_detach();

    s_dl.pp_size = bk_image_size_get(cfg->width,
                                     16U * DL_SEG_HEIGHT_MB * DL_SEG_NUM,
                                     BK_PIXEL_FORMAT_NV12);
    s_dl.pp_buf = (uint8_t *)dl_hsram_aligned_malloc(64U, s_dl.pp_size);
    if (s_dl.pp_buf == NULL)
    {
        LOGE("alloc pp_buf (%u) failed\n", (unsigned)s_dl.pp_size);
        return BK_ERR_NO_MEM;
    }
    os_memset(s_dl.pp_buf, 0, s_dl.pp_size);

    /* Coded AU slots: a compressed frame is well under the raw NV12 size. */
    slot_capacity = (uint32_t)cfg->width * (uint32_t)cfg->height;
    bk_ret = doorbell_downlink_img_manager_init(DL_SLOT_COUNT, slot_capacity);
    if (bk_ret != BK_OK)
    {
        LOGE("img manager init failed=%d\n", bk_ret);
        goto fail_pp;
    }

#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
    /* Slots exist now: point the network reassembly at them. */
    dl_zerocopy_register();
#endif

    /* Compose PIP top-right of the DISPLAY buffer.
     *
     * The GPU blit overlay (gpu_flex_data_frame_done_blit) composites into the
     * POST-rotation display buffer: for a 90/270 main rotation its dimensions
     * are swapped to (dst_h x dst_w). The blit is itself rotated by pip_rotate,
     * so a DL_PIP_WIDTH x DL_PIP_HEIGHT source occupies a transposed
     * DL_PIP_HEIGHT x DL_PIP_WIDTH footprint. Both the anchor base AND the
     * footprint must therefore be expressed in that rotated display space, or
     * the window lands outside the buffer width and only stride-wrap fragments
     * render. */
    board = app_gpu_board_config_get();
    if (board != NULL && board->flexa.dst_width != 0U && board->flexa.dst_height != 0U)
    {
        dst_w = board->flexa.dst_width;
        dst_h = board->flexa.dst_height;
    }
    comp_cfg.main_width = cfg->width;
    comp_cfg.main_height = cfg->height;
    comp_cfg.pip_enable = (doorbell_devices_isp_handle_get() != NULL);
    comp_cfg.pip_width = DL_PIP_WIDTH;
    comp_cfg.pip_height = DL_PIP_HEIGHT;
    comp_cfg.pip_rotate = (board != NULL) ? board->flexa.degree : 90U;
    {
        bool rot = (comp_cfg.pip_rotate == 90U || comp_cfg.pip_rotate == 270U);
        /* Display-buffer dimensions (post main rotation). */
        uint16_t disp_w = rot ? dst_h : dst_w;
        uint16_t disp_h = rot ? dst_w : dst_h;
        /* PIP footprint in that display space (post blit rotation). */
        uint16_t vis_w = rot ? DL_PIP_HEIGHT : DL_PIP_WIDTH;
        uint16_t vis_h = rot ? DL_PIP_WIDTH : DL_PIP_HEIGHT;
        /* Right-anchored, top margin (WeChat-style self-view). */
        comp_cfg.pip_dst_x = (disp_w > (vis_w + DL_PIP_MARGIN)) ? (disp_w - vis_w - DL_PIP_MARGIN) : 0U;
        comp_cfg.pip_dst_y = (disp_h > (vis_h + DL_PIP_MARGIN)) ? DL_PIP_MARGIN : 0U;
    }

    bk_ret = doorbell_compositor_start(&comp_cfg, s_dl.pp_buf, DL_SEG_NUM);
    if (bk_ret != BK_OK)
    {
        LOGE("compositor start failed=%d\n", bk_ret);
        goto fail_mgr;
    }

    dec_cfg.timeout_ms = DL_DECODE_TIMEOUT_MS;
    dec_cfg.out_width = cfg->width;
    dec_cfg.out_height = cfg->height;
    dec_cfg.out_format = BK_PIXEL_FORMAT_NV12;
    dec_cfg.segment_height = DL_SEG_HEIGHT_MB;
    dec_cfg.segment_number = DL_SEG_NUM;

    ret = bk_h264_decode_flexa_ctlr_new(&s_dl.decoder, &dec_cfg);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("decoder new failed=%d\n", (int)ret);
        goto fail_comp;
    }
    if (bk_h264_decode_init(s_dl.decoder) != AVDK_ERR_OK ||
        bk_h264_decode_open(s_dl.decoder) != AVDK_ERR_OK)
    {
        LOGE("decoder init/open failed\n");
        goto fail_dec;
    }

    ret = bk_flexa_h264d_gpu_bond_start(&s_dl.bond, s_dl.decoder,
                                        doorbell_compositor_gpu_handle_get());
    if (ret != AVDK_ERR_OK)
    {
        LOGE("h264d->gpu bond failed=%d\n", (int)ret);
        goto fail_dec;
    }

    if (s_dl.done_sem == NULL)
    {
        if (rtos_init_semaphore(&s_dl.done_sem, 1) != BK_OK)
        {
            goto fail_bond;
        }
    }

    s_dl.running = 1U;
    s_dl.stop_request = 0U;

    bk_ret = rtos_create_thread(&s_dl.task, DL_TASK_PRIORITY, "db_h264d",
                                (beken_thread_function_t)dl_decode_task_entry,
                                DL_TASK_STACK, NULL);
    if (bk_ret != BK_OK)
    {
        LOGE("create decode task failed=%d\n", bk_ret);
        s_dl.running = 0U;
        rtos_deinit_semaphore(&s_dl.done_sem);
        s_dl.done_sem = NULL;
        goto fail_bond;
    }

    LOGI("downlink video started, %ux%u fps=%u\n",
         (unsigned)cfg->width, (unsigned)cfg->height, (unsigned)cfg->fps);
    return BK_OK;

fail_bond:
    if (s_dl.bond != NULL)
    {
        bk_flexa_h264d_gpu_bond_stop(s_dl.bond);
        s_dl.bond = NULL;
    }
fail_dec:
    if (s_dl.decoder != NULL)
    {
        (void)bk_h264_decode_close(s_dl.decoder);
        (void)bk_h264_decode_deinit(s_dl.decoder);
        (void)bk_h264_decode_delete(s_dl.decoder);
        s_dl.decoder = NULL;
    }
fail_comp:
    doorbell_compositor_stop();
fail_mgr:
    doorbell_downlink_img_manager_deinit();
fail_pp:
    if (s_dl.pp_buf != NULL)
    {
        dl_hsram_aligned_free(s_dl.pp_buf);
        s_dl.pp_buf = NULL;
    }
    s_dl.pp_size = 0U;
    /* Startup failed: restore the single-view preview we detached above. */
    (void)doorbell_devices_preview_gpu_attach();
    return BK_FAIL;
}

bk_err_t doorbell_downlink_set_h264_receive_config(const doorbell_downlink_h264_config_t *cfg)
{
    if (cfg == NULL || cfg->width == 0U || cfg->height == 0U)
    {
        return BK_ERR_PARAM;
    }

    if (s_dl.running != 0U)
    {
        if (s_dl.cfg.width == cfg->width && s_dl.cfg.height == cfg->height)
        {
            /* Same geometry -> just adopt hint fields, keep pipeline running. */
            s_dl.cfg = *cfg;
            return BK_OK;
        }
        (void)doorbell_downlink_video_stop();
    }

    return dl_start(cfg);
}

bool doorbell_downlink_video_is_running(void)
{
    return (s_dl.running != 0U);
}

int doorbell_downlink_video_recv(uint8_t *data, uint32_t length)
{
    downlink_frame_t *frame;

    if (s_dl.running == 0U || data == NULL || length == 0U)
    {
        return BK_OK;
    }

    frame = doorbell_downlink_free_request();
    if (frame == NULL)
    {
        /* Consumer behind: drop this frame. */
        return BK_OK;
    }
    if (length > frame->capacity)
    {
        LOGW("frame %u > slot cap %u, drop\n", (unsigned)length, (unsigned)frame->capacity);
        (void)doorbell_downlink_free_push(frame);
        return BK_OK;
    }

    os_memcpy(frame->data, data, length);
    frame->size = length;
    (void)doorbell_downlink_ready_push(frame);
    return BK_OK;
}
