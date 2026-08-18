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
 * Boot video player orchestrator.
 *
 * Flow (runs on a dedicated worker thread; boot_video_play() returns at once):
 *   auto-mount fs (by path prefix)
 *   -> stat file
 *   -> probe media info (short-lived probe engine, container parser only)
 *   -> resolve rotation
 *   -> engine_new (audio callbacks only wired when the file has audio)
 *   -> register matching container parser + video decoder (+ aac if audio)
 *   -> turn LCD on + start display worker
 *   -> engine_open -> (open audio output if audio) -> set_file_path -> play
 *   -> wait for playback finished / stop
 *   -> tear down in reverse, turn LCD off (per display_mode), auto-unmount fs
 */

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include <components/bk_video_player/bk_video_player_types.h>
#include <components/bk_video_player/bk_video_player_engine.h>
#include <components/bk_video_player/container_parser/bk_video_player_avi_parser.h>
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_MP4_PARSER
#include <components/bk_video_player/container_parser/bk_video_player_mp4_parser.h>
#endif
#include <components/bk_video_player/video_decoder/bk_video_player_hw_jpeg_decoder.h>
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
#include <components/bk_video_player/video_decoder/bk_video_player_hw_h264_decoder.h>
#endif
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_AAC_AUDIO_DECODER
#include <components/bk_video_player/audio_decoder/bk_video_player_aac_decoder.h>
#endif

#include "boot_video_player.h"
#include "boot_video_priv.h"
#include "boot_video_fs.h"

#ifndef CONFIG_BOOT_VIDEO_TASK_PRIORITY
#define CONFIG_BOOT_VIDEO_TASK_PRIORITY   (4)
#endif
#ifndef CONFIG_BOOT_VIDEO_TASK_STACK_SIZE
#define CONFIG_BOOT_VIDEO_TASK_STACK_SIZE (4096)
#endif

#define BOOT_VIDEO_FINISH_POLL_MS         (100U)

/* On BK7259 QFN128 EVB the SD card VDD is always on (VBAT -> VDD SD/QSPI); there
 * is no GPIO-controlled SD LDO. Cold-boot mount can still fail CMD8 when the
 * SDIO pull-ups parasitically feed the card before enumeration. Retry the mount
 * on the worker thread until the card is ready. */
#define BOOT_VIDEO_FS_MOUNT_RETRY         (8U)
#define BOOT_VIDEO_FS_MOUNT_RETRY_DELAY_MS (300U)

typedef struct
{
    boot_video_play_cfg_t          cfg;         /* deep copy (file_path duplicated) */
    char                          *file_path;   /* owned copy of cfg.file_path */
    boot_video_ctx_t               ctx;
    bk_video_player_engine_handle_t engine;
    boot_video_fs_t                fs;
    bool                           fs_valid;

    beken_thread_t                 thread;
    beken_semaphore_t              finish_sem;
    volatile bool                  playing;
    volatile bool                  stop_req;
} boot_video_state_t;

static boot_video_state_t s_bv;
static beken_mutex_t s_bv_lock = NULL;

/* ------------------------------------------------------------------ utils */

static avdk_err_t boot_video_lock_init(void)
{
    if (s_bv_lock == NULL)
    {
        if (rtos_init_mutex(&s_bv_lock) != BK_OK)
        {
            return AVDK_ERR_GENERIC;
        }
    }
    return AVDK_ERR_OK;
}

static bool boot_video_path_has_ext(const char *path, const char *ext)
{
    size_t plen = os_strlen(path);
    size_t elen = os_strlen(ext);
    if (plen < elen)
    {
        return false;
    }
    const char *p = path + (plen - elen);
    for (size_t i = 0; i < elen; i++)
    {
        char a = p[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z')
        {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z')
        {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

/* Register the single container parser matching the file extension. */
static avdk_err_t boot_video_register_container_parser(bk_video_player_engine_handle_t engine,
                                                       const char *path)
{
    if (boot_video_path_has_ext(path, ".mp4"))
    {
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_MP4_PARSER
        video_player_container_parser_ops_t *ops = bk_video_player_get_mp4_parser_ops();
        if (ops == NULL)
        {
            BOOT_VIDEO_LOGE("%s: mp4 parser ops NULL\n", __func__);
            return AVDK_ERR_UNSUPPORTED;
        }
        return bk_video_player_engine_register_container_parser(engine, ops);
#else
        BOOT_VIDEO_LOGW("%s: MP4 boot video but MP4 parser disabled (enable "
                        "CONFIG_BK_VIDEO_PLAYER_ENABLE_MP4_PARSER)\n", __func__);
        return AVDK_ERR_UNSUPPORTED;
#endif
    }

    if (boot_video_path_has_ext(path, ".avi"))
    {
        video_player_container_parser_ops_t *ops = bk_video_player_get_avi_parser_ops();
        if (ops == NULL)
        {
            BOOT_VIDEO_LOGE("%s: avi parser ops NULL\n", __func__);
            return AVDK_ERR_UNSUPPORTED;
        }
        return bk_video_player_engine_register_container_parser(engine, ops);
    }

    BOOT_VIDEO_LOGE("%s: unsupported container extension: %s\n", __func__, path);
    return AVDK_ERR_UNSUPPORTED;
}

/* Register the video decoder required by the probed video format. */
static avdk_err_t boot_video_register_video_decoder(bk_video_player_engine_handle_t engine,
                                                    video_player_video_format_t format)
{
    if (format == VIDEO_PLAYER_VIDEO_FORMAT_MJPEG)
    {
        video_player_video_decoder_ops_t *ops = bk_video_player_get_hw_jpeg_decoder_ops();
        if (ops == NULL)
        {
            BOOT_VIDEO_LOGE("%s: hw jpeg decoder ops NULL\n", __func__);
            return AVDK_ERR_UNSUPPORTED;
        }
        return bk_video_player_engine_register_video_decoder(engine, ops);
    }

    if (format == VIDEO_PLAYER_VIDEO_FORMAT_H264)
    {
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
        /* Frame-zerocopy (raw NV12) path: the H264 IP decodes whole NV12 frames
         * into its own pool, the DPU shows them uncompressed. This avoids the
         * Flexa+GPU compressed-ARGB8888 path, whose DEC400 tile alignment fails
         * for non-64-aligned widths like 1080 ("VG: dec align error"). */
        video_player_video_decoder_ops_t *ops = bk_video_player_get_hw_h264_decoder_frame_zerocopy_ops();
        if (ops == NULL)
        {
            BOOT_VIDEO_LOGE("%s: hw h264 frame-zerocopy decoder ops NULL\n", __func__);
            return AVDK_ERR_UNSUPPORTED;
        }
        return bk_video_player_engine_register_video_decoder(engine, ops);
#else
        BOOT_VIDEO_LOGW("%s: H264 boot video but HW H264 decoder disabled\n", __func__);
        return AVDK_ERR_UNSUPPORTED;
#endif
    }

    BOOT_VIDEO_LOGE("%s: unsupported video format: %d\n", __func__, format);
    return AVDK_ERR_UNSUPPORTED;
}

static void boot_video_fill_common_cfg(bk_video_player_config_t *cfg)
{
    os_memset(cfg, 0, sizeof(*cfg));

    cfg->video.parser_to_decode_buffer_count = 2;
    cfg->video.decode_to_output_buffer_count = 2;
    cfg->video.packet_buffer_alloc_cb = boot_video_video_packet_buffer_alloc_cb;
    cfg->video.packet_buffer_free_cb  = boot_video_video_packet_buffer_free_cb;
    cfg->video.buffer_alloc_cb        = boot_video_video_buffer_alloc_yuv_cb;
    cfg->video.buffer_free_cb         = boot_video_video_buffer_free_yuv_cb;
    cfg->video.output_format          = PIXEL_FMT_NV12;
}

/*
 * Probe media info using a short-lived engine that only has the matching
 * container parser registered (no decoders / no display). Needed up front so
 * AUTO rotation and the "has audio" decision are known before the real engine
 * is created (config.video.rotate_degree is captured at engine_new).
 */
static avdk_err_t boot_video_probe_media_info(const char *path, video_player_media_info_t *info)
{
    bk_video_player_config_t cfg;
    boot_video_fill_common_cfg(&cfg);

    bk_video_player_engine_handle_t probe = NULL;
    avdk_err_t ret = bk_video_player_engine_new(&probe, &cfg);
    if (ret != AVDK_ERR_OK || probe == NULL)
    {
        BOOT_VIDEO_LOGE("%s: probe engine_new failed, ret=%d\n", __func__, ret);
        return (ret != AVDK_ERR_OK) ? ret : AVDK_ERR_GENERIC;
    }

    ret = boot_video_register_container_parser(probe, path);
    if (ret != AVDK_ERR_OK)
    {
        bk_video_player_engine_delete(probe);
        return ret;
    }

    /* get_media_info() locks active_mutex, which is only initialized in engine_open(). */
    ret = bk_video_player_engine_open(probe);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_VIDEO_LOGE("%s: probe engine_open failed, ret=%d\n", __func__, ret);
        bk_video_player_engine_delete(probe);
        return ret;
    }

    ret = bk_video_player_engine_get_media_info(probe, path, info);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_VIDEO_LOGE("%s: get_media_info failed, ret=%d, file=%s\n", __func__, ret, path);
    }

    (void)bk_video_player_engine_close(probe);
    bk_video_player_engine_delete(probe);
    return ret;
}

static boot_video_rotate_mode_t boot_video_degree_to_mode(uint32_t degree)
{
    degree %= 360U;
    if (degree == 90U)
    {
        return BOOT_VIDEO_ROTATE_90;
    }
    if (degree == 270U)
    {
        return BOOT_VIDEO_ROTATE_270;
    }
    /* 0 and (unsupported) 180 fall back to no GPU rotation. */
    return BOOT_VIDEO_ROTATE_NONE;
}

static uint32_t boot_video_resolve_rotate_degree(uint32_t requested,
                                                 const video_player_media_info_t *info,
                                                 uint16_t panel_w, uint16_t panel_h)
{
    if (requested != BOOT_VIDEO_ROTATE_AUTO)
    {
        return requested % 360U;
    }

    uint32_t vw = info->video.width;
    uint32_t vh = info->video.height;
    if (vw == 0U || vh == 0U || panel_w == 0U || panel_h == 0U)
    {
        return 0U;
    }
    /* Rotate 90 when video orientation differs from the panel orientation. */
    return ((vw > vh) != (panel_w > panel_h)) ? 90U : 0U;
}

/* ------------------------------------------------------ playback finished */

static void boot_video_on_finished(void *user_data, const char *file_path)
{
    (void)user_data;
    (void)file_path;
    if (s_bv.finish_sem != NULL)
    {
        rtos_set_semaphore(&s_bv.finish_sem);
    }
}

/* ---------------------------------------------------------------- teardown */

static void boot_video_teardown(bk_err_t result)
{
    /* Reverse order: engine -> display worker -> audio -> LCD -> fs. */
    if (s_bv.engine != NULL)
    {
        (void)bk_video_player_engine_stop(s_bv.engine);
        (void)bk_video_player_engine_close(s_bv.engine);
        (void)bk_video_player_engine_delete(s_bv.engine);
        s_bv.engine = NULL;
    }

    boot_video_display_worker_deinit();

    if (s_bv.ctx.audio_player_handle != NULL)
    {
        boot_video_audio_close(s_bv.ctx.audio_player_handle);
        s_bv.ctx.audio_player_handle = NULL;
    }

    if (s_bv.ctx.lcd_handle != NULL)
    {
        if (s_bv.cfg.display_mode == BOOT_VIDEO_DISPLAY_ON_OFF)
        {
            (void)boot_video_lcd_close(s_bv.cfg.display_ops);
        }
        else
        {
            /* KEEP_ON / ASSUME_ON: just drop GPU/format runtime state. */
            boot_video_lcd_runtime_format_reset();
        }
        s_bv.ctx.lcd_handle = NULL;
    }

    if (s_bv.fs_valid && boot_video_fs_was_auto_mounted(s_bv.fs))
    {
        (void)boot_video_fs_unmount(s_bv.fs);
    }

    boot_video_done_cb_t done_cb = s_bv.cfg.done_cb;
    void *user_data = s_bv.cfg.user_data;

    if (s_bv.file_path != NULL)
    {
        os_free(s_bv.file_path);
        s_bv.file_path = NULL;
    }
    if (s_bv.finish_sem != NULL)
    {
        rtos_deinit_semaphore(&s_bv.finish_sem);
        s_bv.finish_sem = NULL;
    }

    s_bv.playing = false;

    if (done_cb != NULL)
    {
        done_cb(result, user_data);
    }

    BOOT_VIDEO_LOGI("%s: boot video finished, result=%d\n", __func__, result);
}

/* ------------------------------------------------------------- worker task */

static void boot_video_task(void *arg)
{
    (void)arg;
    const char *path = s_bv.file_path;
    bk_err_t result = BK_FAIL;

    do
    {
        /* 0. mount the backing filesystem (deferred here from boot_video_play so
         *    the main init path is never blocked). SDIO init can fail on the
         *    first cold boot; retry with a short delay until ready. */
        if (s_bv.fs_valid)
        {
            /* Workaround for an SD-card power/routing hardware limitation on this
             * board: on a cold boot the always-powered card can fail CMD8/mount
             * unless it is force-discharged first. No-op for non-SD filesystems or
             * an already-mounted card. See boot_video_fs_sd_cold_boot_reset(). */
            boot_video_fs_sd_cold_boot_reset(s_bv.fs);

            bool was_mounted = boot_video_fs_is_mounted(s_bv.fs);
            bk_err_t mret = BK_FAIL;
            for (uint32_t attempt = 0; attempt < BOOT_VIDEO_FS_MOUNT_RETRY; attempt++)
            {
                if (s_bv.stop_req)
                {
                    break;
                }
                mret = boot_video_fs_mount(s_bv.fs);
                if (mret == BK_OK)
                {
                    break;
                }
                BOOT_VIDEO_LOGW("%s: mount %s failed (attempt %u/%u), retry in %ums\n",
                                __func__, path, (unsigned)(attempt + 1),
                                (unsigned)BOOT_VIDEO_FS_MOUNT_RETRY,
                                (unsigned)BOOT_VIDEO_FS_MOUNT_RETRY_DELAY_MS);
                rtos_delay_milliseconds(BOOT_VIDEO_FS_MOUNT_RETRY_DELAY_MS);
            }
            if (mret != BK_OK)
            {
                BOOT_VIDEO_LOGE("%s: mount fs failed for %s after %u attempts\n",
                                __func__, path, (unsigned)BOOT_VIDEO_FS_MOUNT_RETRY);
                break;
            }
            /* Mark as auto-mounted only if we were the ones who mounted it. */
            boot_video_fs_mark_auto_mounted(s_bv.fs, !was_mounted);
        }

        /* 1. probe media info (container parser only). This opens the file, so a
         *    missing/unreadable file is reported here (no separate stat() needed;
         *    newlib stat() is not backed by VFS on this target). */
        video_player_media_info_t info;
        os_memset(&info, 0, sizeof(info));
        if (boot_video_probe_media_info(path, &info) != AVDK_ERR_OK)
        {
            break;
        }
        if (info.video.format != VIDEO_PLAYER_VIDEO_FORMAT_MJPEG &&
            info.video.format != VIDEO_PLAYER_VIDEO_FORMAT_H264)
        {
            BOOT_VIDEO_LOGW("%s: no supported video track (format=%d)\n", __func__, info.video.format);
            break;
        }
#if !CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
        if (info.video.format == VIDEO_PLAYER_VIDEO_FORMAT_H264)
        {
            BOOT_VIDEO_LOGW("%s: H264 boot video but HW H264 decoder disabled\n", __func__);
            break;
        }
#endif
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_AAC_AUDIO_DECODER
        const bool audio_enabled = (info.audio.channels > 0 && info.audio.sample_rate > 0);
#else
        /* No AAC decoder linked in: never enable audio regardless of the track. */
        const bool audio_enabled = false;
#endif

        /* 3. resolve rotation from panel vs. video orientation. Panel geometry is
         *    supplied by the caller (cfg.panel_width/height) so this component
         *    does not depend on any display service for the panel size. */
        uint16_t panel_w = s_bv.cfg.panel_width;
        uint16_t panel_h = s_bv.cfg.panel_height;
        uint32_t rotate_degree = boot_video_resolve_rotate_degree(s_bv.cfg.rotate_degree,
                                                                  &info, panel_w, panel_h);
        boot_video_video_set_rotate_mode(boot_video_degree_to_mode(rotate_degree));

        /* 4. build the real engine config */
        bk_video_player_config_t cfg;
        boot_video_fill_common_cfg(&cfg);
        cfg.video.decode_complete_cb = boot_video_video_decode_complete_cb;
        cfg.video.rotate_degree = rotate_degree;
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
        /* H264 uses the frame-zerocopy decoder: its NV12 decode pool lives in
         * PSRAM0 (UNCODED), so place the displayable output frames in PSRAM1
         * (CODED) to avoid exhausting PSRAM0. MJPEG keeps the default UNCODED
         * allocator. */
        if (info.video.format == VIDEO_PLAYER_VIDEO_FORMAT_H264)
        {
            cfg.video.buffer_alloc_cb = boot_video_video_buffer_alloc_yuv_coded_cb;
            cfg.video.buffer_free_cb  = boot_video_video_buffer_free_yuv_coded_cb;
        }
#endif
        if (panel_w != 0 && panel_h != 0)
        {
            cfg.video.display_width  = panel_w;
            cfg.video.display_height = panel_h;
        }
        if (audio_enabled)
        {
            cfg.audio.parser_to_decode_buffer_count = 2;
            cfg.audio.decode_to_output_buffer_count = 2;
            cfg.audio.buffer_alloc_cb = boot_video_audio_buffer_alloc_cb;
            cfg.audio.buffer_free_cb  = boot_video_audio_buffer_free_cb;
            cfg.audio.decode_complete_cb = boot_video_audio_decode_complete_cb;
            cfg.audio.audio_set_volume_cb = boot_video_audio_set_volume_cb;
            cfg.audio.audio_set_mute_cb   = boot_video_audio_set_mute_cb;
            cfg.audio.audio_output_config_cb = boot_video_audio_output_config_cb;
            /* audio_*_user_data == NULL -> engine uses cfg.user_data */
        }
        s_bv.ctx.lcd_handle = NULL;
        s_bv.ctx.audio_player_handle = NULL;
        s_bv.ctx.audio_volume = s_bv.cfg.volume;
        s_bv.ctx.audio_muted = s_bv.cfg.mute;
        cfg.user_data = &s_bv.ctx;
        cfg.playback_finished_cb = boot_video_on_finished;

        /* 5. create engine + register modules per media info */
        if (bk_video_player_engine_new(&s_bv.engine, &cfg) != AVDK_ERR_OK || s_bv.engine == NULL)
        {
            BOOT_VIDEO_LOGE("%s: engine_new failed\n", __func__);
            s_bv.engine = NULL;
            break;
        }
        if (boot_video_register_container_parser(s_bv.engine, path) != AVDK_ERR_OK)
        {
            break;
        }
        if (boot_video_register_video_decoder(s_bv.engine, info.video.format) != AVDK_ERR_OK)
        {
            break;
        }
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_AAC_AUDIO_DECODER
        if (audio_enabled)
        {
            const video_player_audio_decoder_ops_t *aac = bk_video_player_get_aac_decoder_ops();
            if (aac == NULL ||
                bk_video_player_engine_register_audio_decoder(s_bv.engine, aac) != AVDK_ERR_OK)
            {
                BOOT_VIDEO_LOGW("%s: register aac decoder failed, play muted\n", __func__);
            }
        }
#endif

        if (s_bv.stop_req)
        {
            break;
        }

        /* 6. turn LCD on + start display worker. Power/handle come from the
         *    injected display ops; for ASSUME_ON the caller's lcd_open is expected
         *    to be a no-op turn-on that just returns the handle. */
        boot_video_lcd_fmt_t lcd_fmt = boot_video_lcd_format_for_video_codec(info.video.format);
        if (boot_video_lcd_open_with_format(s_bv.cfg.display_ops, &s_bv.ctx.lcd_handle, lcd_fmt) != AVDK_ERR_OK)
        {
            BOOT_VIDEO_LOGE("%s: LCD open failed\n", __func__);
            break;
        }

        if (boot_video_display_worker_init() != AVDK_ERR_OK)
        {
            break;
        }

        /* 7. open engine */
        if (bk_video_player_engine_open(s_bv.engine) != AVDK_ERR_OK)
        {
            BOOT_VIDEO_LOGE("%s: engine_open failed\n", __func__);
            break;
        }

        /* 8. open audio output (only when the file has audio) */
        if (audio_enabled)
        {
            boot_video_audio_handle_t ah = NULL;
            if (boot_video_audio_open(&info, s_bv.cfg.volume, s_bv.cfg.mute, &ah) == AVDK_ERR_OK && ah != NULL)
            {
                s_bv.ctx.audio_player_handle = ah;
                (void)bk_video_player_engine_set_volume(s_bv.engine, s_bv.cfg.volume);
                (void)bk_video_player_engine_set_mute(s_bv.engine, s_bv.cfg.mute);
            }
            else
            {
                BOOT_VIDEO_LOGW("%s: audio output init failed, play muted\n", __func__);
            }
        }

        if (s_bv.stop_req)
        {
            break;
        }

        /* 9. set path + play */
        if (bk_video_player_engine_set_file_path(s_bv.engine, path) != AVDK_ERR_OK)
        {
            BOOT_VIDEO_LOGE("%s: set_file_path failed\n", __func__);
            break;
        }
        if (bk_video_player_engine_play(s_bv.engine) != AVDK_ERR_OK)
        {
            BOOT_VIDEO_LOGE("%s: play failed\n", __func__);
            break;
        }

        BOOT_VIDEO_LOGI("%s: playing %s (video_fmt=%d, audio=%d, rotate=%u)\n",
                        __func__, path, info.video.format, audio_enabled, (unsigned)rotate_degree);

        /* 10. wait for finished or stop */
        while (!s_bv.stop_req)
        {
            if (s_bv.finish_sem != NULL &&
                rtos_get_semaphore(&s_bv.finish_sem, BOOT_VIDEO_FINISH_POLL_MS) == BK_OK)
            {
                break;
            }
        }
        result = BK_OK;
    } while (0);

    boot_video_teardown(result);
    s_bv.thread = NULL;
    rtos_delete_thread(NULL);
}

/* ------------------------------------------------------------- public API */

bk_err_t boot_video_play(const boot_video_play_cfg_t *cfg)
{
    if (cfg == NULL || cfg->file_path == NULL || cfg->file_path[0] == '\0')
    {
        return BK_ERR_PARAM;
    }
    if (cfg->display_ops == NULL || cfg->display_ops->lcd_open == NULL)
    {
        BOOT_VIDEO_LOGE("%s: display_ops (with lcd_open) is required\n", __func__);
        return BK_ERR_PARAM;
    }
    if (boot_video_lock_init() != AVDK_ERR_OK)
    {
        return BK_FAIL;
    }

    rtos_lock_mutex(&s_bv_lock);
    if (s_bv.playing)
    {
        rtos_unlock_mutex(&s_bv_lock);
        BOOT_VIDEO_LOGW("%s: already playing\n", __func__);
        return BK_ERR_BUSY;
    }

    os_memset(&s_bv, 0, sizeof(s_bv));
    s_bv.cfg = *cfg;

    size_t len = os_strlen(cfg->file_path);
    s_bv.file_path = (char *)os_malloc(len + 1);
    if (s_bv.file_path == NULL)
    {
        rtos_unlock_mutex(&s_bv_lock);
        return BK_ERR_NO_MEM;
    }
    os_memcpy(s_bv.file_path, cfg->file_path, len + 1);
    s_bv.cfg.file_path = s_bv.file_path;

    /* Resolve the backing filesystem by path prefix now, but defer the actual
     * mount to the worker thread: cold-boot SDIO init can fail once, and
     * retrying here would block the main init path. */
    if (boot_video_fs_from_path(s_bv.file_path, &s_bv.fs) == BK_OK)
    {
        s_bv.fs_valid = true;
    }
    else
    {
        BOOT_VIDEO_LOGW("%s: path prefix not recognized, assuming already mounted: %s\n",
                        __func__, s_bv.file_path);
        s_bv.fs_valid = false;
    }

    if (rtos_init_semaphore(&s_bv.finish_sem, 1) != BK_OK)
    {
        if (s_bv.fs_valid && boot_video_fs_was_auto_mounted(s_bv.fs))
        {
            (void)boot_video_fs_unmount(s_bv.fs);
        }
        os_free(s_bv.file_path);
        s_bv.file_path = NULL;
        rtos_unlock_mutex(&s_bv_lock);
        return BK_FAIL;
    }

    s_bv.stop_req = false;
    s_bv.playing = true;

    if (rtos_create_thread(&s_bv.thread, CONFIG_BOOT_VIDEO_TASK_PRIORITY, "boot_video",
                           (beken_thread_function_t)boot_video_task,
                           CONFIG_BOOT_VIDEO_TASK_STACK_SIZE, NULL) != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: create thread failed\n", __func__);
        s_bv.playing = false;
        rtos_deinit_semaphore(&s_bv.finish_sem);
        s_bv.finish_sem = NULL;
        if (s_bv.fs_valid && boot_video_fs_was_auto_mounted(s_bv.fs))
        {
            (void)boot_video_fs_unmount(s_bv.fs);
        }
        os_free(s_bv.file_path);
        s_bv.file_path = NULL;
        rtos_unlock_mutex(&s_bv_lock);
        return BK_FAIL;
    }

    rtos_unlock_mutex(&s_bv_lock);
    return BK_OK;
}

bk_err_t boot_video_stop(void)
{
    if (!s_bv.playing)
    {
        return BK_OK;
    }

    s_bv.stop_req = true;
    if (s_bv.engine != NULL)
    {
        (void)bk_video_player_engine_stop(s_bv.engine);
    }
    if (s_bv.finish_sem != NULL)
    {
        rtos_set_semaphore(&s_bv.finish_sem);
    }
    return BK_OK;
}

bool boot_video_is_playing(void)
{
    return s_bv.playing;
}
