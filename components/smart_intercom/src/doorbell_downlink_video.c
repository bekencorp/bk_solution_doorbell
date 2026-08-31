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
#include "doorbell_jsonrpc.h"
#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
#include "network_transfer.h"
#endif

#define TAG "db-dl-vid"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define DL_SEG_HEIGHT_MB     1U
/* Shallow FLEXA ring depth for downlink frames whose full-frame ring would
 * exceed the HSRAM budget (~128KB alongside the GPU composite buffer).
 * Matches h264d_gpu_display_example (SEG_NUM=4 at 1280x720). */
#define DL_SEG_NUM_SHALLOW   4U
/* 1080p downlink shares the 512KB AP HSRAM heap with the concurrent uplink ISP
 * SP channel (~90KB) + H.264 encoder (task + recon) + transfer task.
 * Measured HSRAM budget for the 1080p ring:
 *   seg=4 (180KB): post-dl free ~148KB -> encoder task fails (-12), min 1136B.
 *   seg=3 (135KB): post-dl free ~198KB -> encoder OK; the last uplink task
 *                  (trs_task, 4KB) originally overflowed HSRAM, now moved off
 *                  HSRAM (see doorbell_devices_start) so the chain fits. Downlink
 *                  decodes fine at seg=3.
 *   seg=2 ( 90KB): HSRAM plenty, BUT the 1080p FLEXA H264 decode<->GPU bond
 *                  needs >=3 ring segments: at seg=2 every frame hits
 *                  "vcdec decode timeout irq_status=0x401" (decoder stalls
 *                  waiting for the 2-segment ring to drain). Do NOT use 2.
 * So 1080p uses seg=3 (the minimum that both decodes AND leaves HSRAM room once
 * trs_task is off HSRAM). 720p keeps seg=4 (already has HSRAM headroom). */
#define DL_SEG_NUM_SHALLOW_1080  3U
/* Height (px) at/above which the 1080p-class shallow ring depth is used. */
#define DL_SEG_1080_HEIGHT_MIN   1080U
/* Full-frame ring budget: width * (16 * frame_segs) * 3/2 above this -> shallow. */
#define DL_SEG_RING_BUDGET   (128U * 1024U)
/* Downlink decode ring depth. Configurable via Kconfig (range 3..8, default 4);
 * see CONFIG_SMART_INTERCOM_DL_SLOT_COUNT for the sizing rationale. Fall back to
 * 4 if the symbol is somehow undefined. */
#ifdef CONFIG_SMART_INTERCOM_DL_SLOT_COUNT
#define DL_SLOT_COUNT        ((uint32_t)CONFIG_SMART_INTERCOM_DL_SLOT_COUNT)
#else
#define DL_SLOT_COUNT        4U
#endif
/* Lower bound for a coded-AU slot, protecting tiny resolutions. The real slot
 * size is resolution-driven (see dl_start): a raw NV12 frame (W*H*3/2) is a
 * guaranteed upper bound for any coded H.264 AU, so the slot never overflows
 * yet stays far smaller than the transport's fixed JPEG_FRAME_SIZE(100KB). The
 * transport's frame_size is only a hint to malloc_cb; our zero-copy allocator
 * owns the real buffer and ignores that hint. */
#define DL_SLOT_CAPACITY_MIN (16U * 1024U)
/* Max coded H.264 AU size (matches uplink encoder / mds_img_manager). At 720p
 * the raw NV12 frame is ~1.38MB but Annex-B access units are far smaller. For a
 * 1080p downlink build the App's key frames (IDR) can exceed the 720p ~460KB
 * cap, so give generous headroom to avoid dropping them; the 720p path keeps the
 * stock cap derived from the uplink encoder Kconfig. */
#if defined(CONFIG_SMART_INTERCOM_DL_RES_1080P)
#define DL_H264_AU_CAP_MAX   (1024U * 1024U)
#elif !defined(CONFIG_BK_ENCODER_H264_MAX_OUTPUT_BUFFER)
#define DL_H264_AU_CAP_MAX   (512U * 1024U)
#else
#define DL_H264_AU_CAP_MAX   ((uint32_t)CONFIG_BK_ENCODER_H264_MAX_OUTPUT_BUFFER)
#endif
#define DL_DECODE_TIMEOUT_MS 1000U
#define DL_POP_TIMEOUT_MS    100U
/* Min interval between doorbell.notify.requestKeyFrame sends (>=300ms)
 * to avoid an IDR-request storm when frames keep being lost. */
#define DL_KEYREQ_MIN_INTERVAL_MS 300U

#define DL_TASK_PRIORITY     3U
#define DL_TASK_STACK        (1024U * 16U)

/* Downlink runtime stats dump period. One line per second (matches the
 * MONITOR / video-fps cadence) so a static on-screen picture can be diagnosed:
 * wifi=0 -> not receiving, h264 ok=0 -> receiving but not decoding. */
#define DL_STATS_DUMP_INTERVAL_MS 5000U

/* Default local self-view (PIP) geometry. */
/* PIP self-view window. Reduced to 320x180 (was 640x360) to cut the PIP
 * compositing cost: with the dual-720p pipeline (downlink decode + GPU
 * composite + uplink ISP MP + H.264 encoder) the 640x360@15fps self-view added
 * ~5MB/s of PSRAM traffic + a large per-frame GPU blit, inflating the main
 * compositor frame time to 150ms-2s and starving the downlink slots (ref-break
 * / wait-IDR storm). 320x180 is 1/4 the pixels -> ~4x less SP write, PSRAM read
 * and blit work, while keeping the self-view at full 15fps.
 * MUST match camera_board.isp.sp_width/height in ap_main.c. SP is non-flexa, so
 * only WIDTH needs 16-alignment for GPU/DMA stride (320/16=20) and both dims
 * must be even for NV12 (180 is even). The GPU blit does NOT scale (blit config
 * has no dst_width/height), so the on-screen window is drawn 1:1 and is now half
 * the linear size; at 90/270 blit rotation a 320x180 source occupies a 180x320
 * footprint, right-anchored at (disp_w-180-32, 32) in the display space. */
#define DL_PIP_WIDTH         320U
#define DL_PIP_HEIGHT        180U
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
    uint8_t pip_deferred;  /* uplink+720p: PIP held until first decode succeeds */
    volatile uint8_t ref_break_pending; /* set on slot-starvation drop */

    /* Runtime statistics. Counters are free-running cumulative and are bumped
     * from the network producer (wifi_*) and the decode task (dec_*); the 1s
     * stats timer prints per-window deltas against the *_cached snapshots. Simple
     * ++/read races only ever lose/skew a single count per window, which is fine
     * for a diagnostic frame-rate readout (same lock-free approach as video_fps). */
    beken_timer_t stats_timer;
    uint8_t  stats_timer_on;
    volatile uint32_t st_wifi_frames;   /* complete AUs received over wifi     */
    volatile uint32_t st_wifi_bytes;    /* their total byte count              */
    volatile uint32_t st_dec_ok;        /* successful h264 decodes             */
    volatile uint32_t st_dec_fail;      /* failed decodes (broken reference)   */
    volatile uint32_t st_dec_skip;      /* AUs skipped while waiting for IDR    */
    uint32_t st_wifi_frames_c;          /* cached snapshots for delta compute  */
    uint32_t st_wifi_bytes_c;
    uint32_t st_dec_ok_c;
    uint32_t st_dec_fail_c;
    uint32_t st_dec_skip_c;
} dl_video_ctx_t;

static dl_video_ctx_t s_dl = {0};

void doorbell_downlink_video_notify_ref_break(void)
{
    if (s_dl.running != 0U)
    {
        s_dl.ref_break_pending = 1U;
    }
}

void doorbell_downlink_video_stats_on_recv(uint32_t bytes)
{
    s_dl.st_wifi_frames++;
    s_dl.st_wifi_bytes += bytes;
}

/* 1s timer: emit one dense line of downlink receive/decode rates. Always prints
 * while running -- a "wifi[0f 0kbps]" or "h264[ok 0f ...]" line is precisely the
 * signal needed when the on-screen picture is frozen, so zero windows are NOT
 * suppressed (unlike video_fps.c, which is a per-transfer TX probe). */
static void dl_stats_dump(void *param)
{
    (void)param;

    uint32_t wf = s_dl.st_wifi_frames;
    uint32_t wb = s_dl.st_wifi_bytes;
    uint32_t ok = s_dl.st_dec_ok;
    uint32_t fa = s_dl.st_dec_fail;
    uint32_t sk = s_dl.st_dec_skip;

    uint32_t wifi_fps  = wf - s_dl.st_wifi_frames_c;
    uint32_t wifi_kbps = (wb - s_dl.st_wifi_bytes_c) * 8U / 1024U;
    uint32_t dec_fps   = ok - s_dl.st_dec_ok_c;
    uint32_t dec_fail  = fa - s_dl.st_dec_fail_c;
    uint32_t dec_skip  = sk - s_dl.st_dec_skip_c;

    s_dl.st_wifi_frames_c = wf;
    s_dl.st_wifi_bytes_c  = wb;
    s_dl.st_dec_ok_c      = ok;
    s_dl.st_dec_fail_c    = fa;
    s_dl.st_dec_skip_c    = sk;

    LOGI("fps: wifi[%uf %ukbps] h264d[ok %uf fail %u skip %u]\n",
         (unsigned)wifi_fps, (unsigned)wifi_kbps,
         (unsigned)dec_fps, (unsigned)dec_fail, (unsigned)dec_skip);
}

static void dl_stats_start(void)
{
    if (s_dl.stats_timer_on != 0U)
    {
        return;
    }

    s_dl.st_wifi_frames = 0U;
    s_dl.st_wifi_bytes  = 0U;
    s_dl.st_dec_ok      = 0U;
    s_dl.st_dec_fail    = 0U;
    s_dl.st_dec_skip    = 0U;
    s_dl.st_wifi_frames_c = 0U;
    s_dl.st_wifi_bytes_c  = 0U;
    s_dl.st_dec_ok_c      = 0U;
    s_dl.st_dec_fail_c    = 0U;
    s_dl.st_dec_skip_c    = 0U;

    if (rtos_init_timer(&s_dl.stats_timer, DL_STATS_DUMP_INTERVAL_MS,
                        dl_stats_dump, NULL) != BK_OK)
    {
        LOGW("stats timer init failed\n");
        return;
    }
    if (rtos_start_timer(&s_dl.stats_timer) != BK_OK)
    {
        LOGW("stats timer start failed\n");
        rtos_deinit_timer(&s_dl.stats_timer);
        os_memset(&s_dl.stats_timer, 0, sizeof(s_dl.stats_timer));
        return;
    }
    s_dl.stats_timer_on = 1U;
}

static void dl_stats_stop(void)
{
    if (s_dl.stats_timer_on == 0U)
    {
        return;
    }
    if (rtos_is_timer_running(&s_dl.stats_timer))
    {
        rtos_stop_timer(&s_dl.stats_timer);
    }
    rtos_deinit_timer(&s_dl.stats_timer);
    os_memset(&s_dl.stats_timer, 0, sizeof(s_dl.stats_timer));
    s_dl.stats_timer_on = 0U;
}

static void dl_apply_ref_break(uint8_t *wait_idr,
#if CONFIG_SMART_INTERCOM_DL_REQUEST_IDR
                               uint32_t *last_keyreq_ms,
#endif
                               const char *reason)
{
    (void)reason;

    if (s_dl.ref_break_pending == 0U)
    {
        return;
    }
    s_dl.ref_break_pending = 0U;

#if CONFIG_SMART_INTERCOM_DL_WAIT_IDR
    if (wait_idr != NULL)
    {
        *wait_idr = 1U;
    }
#endif

#if CONFIG_SMART_INTERCOM_DL_REQUEST_IDR
    if (last_keyreq_ms != NULL)
    {
        uint32_t now = rtos_get_time();
        if ((now - *last_keyreq_ms) >= DL_KEYREQ_MIN_INTERVAL_MS)
        {
            (void)doorbell_notify_request_keyframe("frameLoss", "h264");
            *last_keyreq_ms = now;
        }
    }
#endif
}

/* Pick FLEXA ring depth from negotiated resolution. Full-frame (height/16 segs)
 * when the ring fits HSRAM; otherwise shallow DL_SEG_NUM_SHALLOW (1280x720 -> 4
 * segs / ~120KB, same as h264d_gpu_display_example). */
static uint32_t dl_seg_num_for_resolution(uint16_t width, uint16_t height)
{
    uint32_t frame_segs = (uint32_t)((height + 15U) / 16U);
    uint32_t full_ring;
    /* 1080p-class uses a shallower ring to leave HSRAM for the concurrent
     * uplink ISP SP + H.264 encoder; 720p and below keep the deeper ring. */
    uint32_t shallow = (height >= DL_SEG_1080_HEIGHT_MIN) ? DL_SEG_NUM_SHALLOW_1080
                                                          : DL_SEG_NUM_SHALLOW;

    if (frame_segs == 0U)
    {
        return shallow;
    }

    full_ring = bk_image_size_get(width, 16U * frame_segs, BK_PIXEL_FORMAT_NV12);
    if (full_ring > DL_SEG_RING_BUDGET)
    {
        return shallow;
    }
    return frame_segs;
}

/* Coded-slot byte capacity: sub-720p keeps the NV12 frame size (tiny, exact);
 * 720p-class uses DL_H264_AU_CAP_MAX so 3 slots ~= 1.38MB not 4.1MB. */
static uint32_t dl_slot_capacity_for_resolution(uint16_t width, uint16_t height)
{
    uint32_t nv12_cap = bk_image_size_get(width, height, BK_PIXEL_FORMAT_NV12);
    uint32_t cap = nv12_cap;

    (void)width;
    if (height >= 720U || nv12_cap > DL_H264_AU_CAP_MAX)
    {
        cap = DL_H264_AU_CAP_MAX;
    }
    if (cap < DL_SLOT_CAPACITY_MIN)
    {
        cap = DL_SLOT_CAPACITY_MIN;
    }
    return cap;
}

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
    /* Error-resilience state: this stream is IPPP with P frames chained to the
     * previous frame (max_num_ref_frames=1), so once any frame is lost the
     * reference chain is broken and every following P decodes to garbage /
     * hardware error until the next IDR. Feeding those broken P frames to the
     * HW decoder also spends CPU and floods the error path, which worsens the
     * backlog that caused the loss. So: after a decode failure (broken ref),
     * enter "wait for IDR" and cheaply discard non-IDR AUs (recycling their
     * slot fast) until an IDR access unit arrives to resynchronize.
     * Gated by CONFIG_SMART_INTERCOM_DL_WAIT_IDR (default on); when off, every
     * AU is fed to the decoder as before. */
#if CONFIG_SMART_INTERCOM_DL_WAIT_IDR
    uint8_t wait_idr = 1U;
#else
    uint8_t wait_idr = 0U;
#endif
    uint32_t skipped = 0U;
#if CONFIG_SMART_INTERCOM_DL_REQUEST_IDR
    /* Last doorbell.notify.requestKeyFrame send time, for >=300ms debounce. */
    uint32_t last_keyreq_ms = 0U;
#endif

    (void)arg;
    LOGI("decode task started, %ux%u\n", (unsigned)s_dl.cfg.width, (unsigned)s_dl.cfg.height);

    while (s_dl.stop_request == 0U)
    {
        downlink_frame_t *frame;
        bk_h264_decode_input_t input = {0};
        avdk_err_t ret;
        uint8_t is_idr = 0U;

        frame = doorbell_downlink_ready_pop(DL_POP_TIMEOUT_MS);
        if (frame == NULL)
        {
            dl_apply_ref_break(&wait_idr,
#if CONFIG_SMART_INTERCOM_DL_REQUEST_IDR
                               &last_keyreq_ms,
#endif
                               "idle");
            continue;
        }

        dl_apply_ref_break(&wait_idr,
#if CONFIG_SMART_INTERCOM_DL_REQUEST_IDR
                           &last_keyreq_ms,
#endif
                           "slot_drop");

        input.stream = frame->data;
        input.stream_len = frame->size;
        input.out_buffer = s_dl.pp_buf;
        input.out_buffer_size = s_dl.pp_size;

        /* Scan the access unit for an IDR slice (NAL unit_type 5), which marks a
         * resync point for the wait-for-IDR gate below. */
        {
            const uint8_t *p = frame->data;
            uint32_t n = frame->size;
            uint32_t i;

            for (i = 0U; (n >= 3U) && (i + 2U < n); i++)
            {
                if (p[i] == 0U && p[i + 1U] == 0U && p[i + 2U] == 1U)
                {
                    if (i + 3U < n)
                    {
                        uint8_t nal_t = (uint8_t)(p[i + 3U] & 0x1fU);
                        if (nal_t == 5U) /* IDR slice -> resync point */
                        {
                            is_idr = 1U;
                        }
                    }
                }
            }
        }

#if CONFIG_SMART_INTERCOM_DL_WAIT_IDR
        /* Resync gate: while waiting for an IDR, drop non-IDR AUs without
         * decoding. Recycling the slot immediately drains the ready ring and
         * lets the producer keep up, and skips the useless broken-reference
         * decode + error dump. */
        if (wait_idr != 0U && is_idr == 0U)
        {
            skipped++;
            s_dl.st_dec_skip++;
#if CONFIG_SMART_INTERCOM_DL_REQUEST_IDR
            /* Escape the wait-for-IDR deadlock. While skipping non-IDR AUs we
             * never reach the decode-failure branch that asks for a key frame,
             * so if the stream carries no near-term IDR (e.g. the channel was
             * (re)opened mid-GOP during an on/off stress test), keep requesting
             * one -- rate limited -- until an IDR arrives and decoding resyncs.
             * Without this the local picture stays frozen forever: the main
             * layer is decode-driven and the PIP self-view is only enabled after
             * the first successful decode, so zero decodes == both frozen. */
            {
                uint32_t now = rtos_get_time();
                if ((now - last_keyreq_ms) >= DL_KEYREQ_MIN_INTERVAL_MS)
                {
                    (void)doorbell_notify_request_keyframe("frameLoss", "h264");
                    last_keyreq_ms = now;
                }
            }
#endif
            doorbell_downlink_free_push(frame);
            continue;
        }
#endif /* CONFIG_SMART_INTERCOM_DL_WAIT_IDR */

        ret = bk_h264_decode_frame(s_dl.decoder, &input);

        if (ret == AVDK_ERR_OK)
        {
            decoded++;
            s_dl.st_dec_ok++;

#if CONFIG_SMART_INTERCOM_DL_WAIT_IDR
            wait_idr = 0U; /* reference chain valid again */
#endif
            if (s_dl.pip_deferred != 0U)
            {
                s_dl.pip_deferred = 0U;
                if (doorbell_downlink_pip_enable() == BK_OK)
                {
                    LOGI("DL720 H2: deferred PIP enabled after first decode\n");
                }
                else
                {
                    LOGW("DL720 H2: deferred PIP enable failed\n");
                }
            }
        }
        else
        {
            /* Broken reference (lost frame) or undecodable IDR. */
            s_dl.st_dec_fail++;
#if CONFIG_SMART_INTERCOM_DL_WAIT_IDR
            wait_idr = 1U; /* wait for the next IDR to resynchronize */
#endif
#if CONFIG_SMART_INTERCOM_DL_REQUEST_IDR
            /* Ask the App to force an immediate IDR (doorbell.notify.requestKeyFrame),
             * rate-limited to avoid an IDR-request storm. Only a
             * genuine decode failure (a lost reference) triggers this, so an
             * undecodable IDR is also covered. With DL_WAIT_IDR on, at most one
             * failure fires per loss event; with it off, the debounce caps sends. */
            {
                uint32_t now = rtos_get_time();
                if ((now - last_keyreq_ms) >= DL_KEYREQ_MIN_INTERVAL_MS)
                {
                    (void)doorbell_notify_request_keyframe("frameLoss", "h264");
                    last_keyreq_ms = now;
                }
            }
#endif
        }

        doorbell_downlink_free_push(frame);
    }

    LOGI("decode task exiting, decoded=%u skipped=%u\n",
         (unsigned)decoded, (unsigned)skipped);
    if (s_dl.done_sem != NULL)
    {
        (void)rtos_set_semaphore(&s_dl.done_sem);
    }
    s_dl.task = NULL;
    rtos_delete_thread(NULL);
}

static void dl_teardown(void)
{
    dl_stats_stop();

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

/* Compute the PIP self-view geometry into a compositor config.
 *
 * The GPU blit overlay composites into the POST-rotation display buffer: for a
 * 90/270 main rotation its dimensions are swapped to (dst_h x dst_w). The blit
 * is itself rotated by pip_rotate, so a DL_PIP_WIDTH x DL_PIP_HEIGHT source
 * occupies a transposed DL_PIP_HEIGHT x DL_PIP_WIDTH footprint. Both the anchor
 * base AND the footprint must be expressed in that rotated display space, or the
 * window lands outside the buffer width and only stride-wrap fragments render.
 *
 * pip_enable reflects whether the local ISP SP self-view source exists right now
 * (camera on). When downlink starts before the camera, pip is off here and gets
 * enabled later via doorbell_downlink_pip_enable(). When uplink is already on
 * and downlink is 720p-class, defer PIP so the H.264 decoder recon pool can
 * allocate before the SP channel (~690KB) is opened. */
static void dl_fill_pip_cfg(doorbell_compositor_config_t *comp_cfg,
                            const doorbell_downlink_h264_config_t *dl_cfg,
                            bool defer_for_memory)
{
    gpu_board_config_t *board = app_gpu_board_config_get();
    uint16_t dst_w = 1920U;
    uint16_t dst_h = 1080U;

    if (board != NULL && board->flexa.dst_width != 0U && board->flexa.dst_height != 0U)
    {
        dst_w = board->flexa.dst_width;
        dst_h = board->flexa.dst_height;
    }
    comp_cfg->pip_enable = (doorbell_devices_isp_handle_get() != NULL);
    if (defer_for_memory && comp_cfg->pip_enable &&
        dl_cfg != NULL && dl_cfg->height >= 720U &&
        doorbell_devices_uplink_active())
    {
        comp_cfg->pip_enable = false;
    }
    comp_cfg->pip_width = DL_PIP_WIDTH;
    comp_cfg->pip_height = DL_PIP_HEIGHT;
    comp_cfg->pip_rotate = (board != NULL) ? board->flexa.degree : 90U;
    {
        bool rot = (comp_cfg->pip_rotate == 90U || comp_cfg->pip_rotate == 270U);
        /* Display-buffer dimensions (post main rotation). */
        uint16_t disp_w = rot ? dst_h : dst_w;
        uint16_t disp_h = rot ? dst_w : dst_h;
        /* PIP footprint in that display space (post blit rotation). */
        uint16_t vis_w = rot ? DL_PIP_HEIGHT : DL_PIP_WIDTH;
        uint16_t vis_h = rot ? DL_PIP_WIDTH : DL_PIP_HEIGHT;
        /* Right-anchored, top margin (WeChat-style self-view). */
        comp_cfg->pip_dst_x = (disp_w > (vis_w + DL_PIP_MARGIN)) ? (disp_w - vis_w - DL_PIP_MARGIN) : 0U;
        comp_cfg->pip_dst_y = (disp_h > (vis_h + DL_PIP_MARGIN)) ? DL_PIP_MARGIN : 0U;
    }
}

static bk_err_t dl_start(const doorbell_downlink_h264_config_t *cfg)
{
    doorbell_compositor_config_t comp_cfg = {0};
    bk_h264_decode_flexa_config_t dec_cfg = DEFAULT_H264_DECODE_FLEXA_CONFIG;
    uint32_t slot_capacity;
    uint32_t seg_num;
    bk_err_t bk_ret;
    avdk_err_t ret;

    s_dl.cfg = *cfg;
    s_dl.pip_deferred = 0U;
    seg_num = dl_seg_num_for_resolution(cfg->width, cfg->height);

    /* Enter intercom mode: hand the GPU (and its HSRAM FLEXA ring) over from the
     * single-view preview to the downlink compositor. The uplink ISP MP -> H264
     * encode bond keeps running; ISP SP feeds the PIP self-view. */
    (void)doorbell_devices_preview_gpu_detach();

    s_dl.pp_size = bk_image_size_get(cfg->width,
                                     16U * DL_SEG_HEIGHT_MB * seg_num,
                                     BK_PIXEL_FORMAT_NV12);
    s_dl.pp_buf = (uint8_t *)dl_hsram_aligned_malloc(64U, s_dl.pp_size);
    if (s_dl.pp_buf == NULL)
    {
        LOGE("DL720 H3: alloc pp_buf (%u) failed seg=%u\n",
             (unsigned)s_dl.pp_size, (unsigned)seg_num);
        return BK_ERR_NO_MEM;
    }
    os_memset(s_dl.pp_buf, 0, s_dl.pp_size);

    /* Coded AU slots: at 720p use the H.264 AU cap (~460KB), not raw NV12
     * (~1.38MB), so img_manager_init stays within MEM_SLAB_HEAP_CODED. */
    slot_capacity = dl_slot_capacity_for_resolution(cfg->width, cfg->height);
    bk_ret = doorbell_downlink_img_manager_init(DL_SLOT_COUNT, slot_capacity);
    if (bk_ret != BK_OK)
    {
        LOGE("DL720 H1: img manager init failed=%d slots=%u cap=%u\n",
             bk_ret, (unsigned)DL_SLOT_COUNT, (unsigned)slot_capacity);
        goto fail_pp;
    }

#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
    /* Slots exist now: point the network reassembly at them. */
    dl_zerocopy_register();
#endif

    /* Main = decoded remote picture; PIP = local ISP SP self-view (off until the
     * camera is on, then enabled at runtime via doorbell_downlink_pip_enable). */
    comp_cfg.main_width = cfg->width;
    comp_cfg.main_height = cfg->height;
    dl_fill_pip_cfg(&comp_cfg, cfg, true);
    s_dl.pip_deferred = (comp_cfg.pip_enable == 0U &&
                         doorbell_devices_isp_handle_get() != NULL &&
                         cfg->height >= 720U &&
                         doorbell_devices_uplink_active()) ? 1U : 0U;
    if (s_dl.pip_deferred != 0U)
    {
        LOGI("DL720 H2: defer PIP until first decode (uplink+720p)\n");
    }

    bk_ret = doorbell_compositor_start(&comp_cfg, s_dl.pp_buf, seg_num);
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
    dec_cfg.segment_number = seg_num;

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

    dl_stats_start();

    LOGI("downlink video started, %ux%u fps=%u seg=%u slots=%u slot_cap=%u\n",
         (unsigned)cfg->width, (unsigned)cfg->height, (unsigned)cfg->fps,
         (unsigned)seg_num, (unsigned)DL_SLOT_COUNT, (unsigned)slot_capacity);
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

bk_err_t doorbell_downlink_pip_enable(void)
{
    doorbell_compositor_config_t comp_cfg = {0};

    if (s_dl.running == 0U || !doorbell_compositor_is_running())
    {
        return BK_ERR_STATE;
    }
    dl_fill_pip_cfg(&comp_cfg, &s_dl.cfg, false);
    if (!comp_cfg.pip_enable)
    {
        /* Local ISP SP self-view source not up yet (camera off). */
        return BK_ERR_STATE;
    }
    LOGI("PIP enable requested on running compositor (isp ready)\n");
    return doorbell_compositor_pip_enable(&comp_cfg);
}

bk_err_t doorbell_downlink_pip_disable(void)
{
    if (!doorbell_compositor_is_running())
    {
        return BK_ERR_STATE;
    }
    /* Local uplink/camera going off: drop the self-view overlay so the small
     * window stops showing the last (now stale) ISP SP frame. Downlink main
     * picture keeps running on the compositor. */
    s_dl.pip_deferred = 0U;
    LOGI("PIP disable requested (uplink/camera off)\n");
    return doorbell_compositor_pip_disable();
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
        /* Consumer behind: drop this frame and break the P-frame ref chain. */
        doorbell_downlink_video_notify_ref_break();
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
