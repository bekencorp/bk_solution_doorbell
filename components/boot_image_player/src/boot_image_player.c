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
 * Boot image player orchestrator.
 *
 * Flow (runs on a dedicated worker thread; boot_image_show() returns at once):
 *   auto-mount fs (by path prefix, with SD cold-boot virtual re-plug)
 *   -> read the whole image file into a DMA-accessible (CODED) buffer
 *   -> detect format (AUTO) / decode to a native-size NV12 frame
 *   -> check the decoded size matches the panel (no rotation/scaling)
 *   -> turn LCD on (injected display_ops->open)
 *   -> flush the raw NV12 frame once
 *   -> hold (display_duration_ms; 0 = until boot_image_stop())
 *   -> tear down in reverse, turn LCD off (display_ops->close per display_mode),
 *      auto-unmount fs
 */

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <unistd.h>

#include <components/bk_frame_buffer.h>

#include "bk_posix.h"
#include "boot_image_player.h"
#include "boot_image_priv.h"
#include "boot_image_fs.h"

#ifndef CONFIG_BOOT_IMAGE_TASK_PRIORITY
#define CONFIG_BOOT_IMAGE_TASK_PRIORITY   (4)
#endif
#ifndef CONFIG_BOOT_IMAGE_TASK_STACK_SIZE
#define CONFIG_BOOT_IMAGE_TASK_STACK_SIZE (4096)
#endif

/* Cold-boot SDIO enumeration can fail once; retry the mount on the worker. */
#define BOOT_IMAGE_FS_MOUNT_RETRY          (8U)
#define BOOT_IMAGE_FS_MOUNT_RETRY_DELAY_MS (300U)

/* Guard against pathological file sizes (a boot JPEG is well under this). */
#define BOOT_IMAGE_MAX_FILE_BYTES          (8U * 1024U * 1024U)

typedef struct
{
    boot_image_play_cfg_t     cfg;         /* deep copy (file_path duplicated) */
    char                     *file_path;   /* owned copy of cfg.file_path */
    boot_image_fs_t           fs;
    bool                      fs_valid;

    bk_display_ctlr_handle_t  lcd_handle;

    beken_thread_t            thread;
    beken_semaphore_t         stop_sem;    /* also used to end the hold early */
    volatile bool             showing;
    volatile bool             stop_req;
} boot_image_state_t;

static boot_image_state_t s_bi;
static beken_mutex_t s_bi_lock = NULL;

/* ------------------------------------------------------------------ utils */

static avdk_err_t boot_image_lock_init(void)
{
    if (s_bi_lock == NULL)
    {
        if (rtos_init_mutex(&s_bi_lock) != BK_OK)
        {
            return AVDK_ERR_GENERIC;
        }
    }
    return AVDK_ERR_OK;
}

/*
 * Read the whole file into a freshly-allocated DMA-accessible (CODED slab)
 * buffer. Returns the buffer (caller frees with bk_frame_buffer_free) and its
 * length via out_len, or NULL on failure.
 */
static uint8_t *boot_image_read_file(const char *path, uint32_t *out_len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        BOOT_IMAGE_LOGE("%s: open %s failed, fd=%d\n", __func__, path, fd);
        return NULL;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    (void)lseek(fd, 0, SEEK_SET);
    if (size <= 0 || (uint32_t)size > BOOT_IMAGE_MAX_FILE_BYTES)
    {
        BOOT_IMAGE_LOGE("%s: bad file size %ld for %s\n", __func__, (long)size, path);
        close(fd);
        return NULL;
    }

    uint8_t *buf = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, (uint32_t)size);
    if (buf == NULL)
    {
        BOOT_IMAGE_LOGE("%s: alloc %ld-byte stream buffer failed\n", __func__, (long)size);
        close(fd);
        return NULL;
    }

    uint32_t done = 0;
    while (done < (uint32_t)size)
    {
        int n = read(fd, buf + done, (uint32_t)size - done);
        if (n <= 0)
        {
            BOOT_IMAGE_LOGE("%s: read %s failed at %u/%ld, n=%d\n",
                            __func__, path, (unsigned)done, (long)size, n);
            bk_frame_buffer_free(buf);
            close(fd);
            return NULL;
        }
        done += (uint32_t)n;
    }

    close(fd);
    *out_len = (uint32_t)size;
    return buf;
}

/* ---------------------------------------------------------------- teardown */

/*
 * Release every resource acquired for the current show. MUST be called with
 * s_bi_lock held (from the worker tail): the caller publishes the IDLE state
 * (s_bi.showing=false) and invokes done_cb afterwards, so teardown itself no
 * longer touches s_bi.showing nor runs the callback. Freeing stop_sem under
 * the lock is what makes boot_image_stop() safe from a stop_sem use-after-free.
 */
static void boot_image_teardown(bk_err_t result)
{
    if (s_bi.lcd_handle != NULL)
    {
        /* NOTE: for KEEP_ON / ASSUME_ON the displayed frame buffer is
         * intentionally retained (the DPU is still scanning it); it is released
         * by the flush free-cb on the next flush or when the panel is finally
         * turned off by the business layer. */
        if (s_bi.cfg.display_mode == BOOT_IMAGE_DISPLAY_ON_OFF &&
            s_bi.cfg.display_ops != NULL && s_bi.cfg.display_ops->lcd_close != NULL)
        {
            (void)s_bi.cfg.display_ops->lcd_close(s_bi.cfg.display_ops->user);
        }
        s_bi.lcd_handle = NULL;
    }

    if (s_bi.fs_valid && boot_image_fs_was_auto_mounted(s_bi.fs))
    {
        (void)boot_image_fs_unmount(s_bi.fs);
    }

    if (s_bi.file_path != NULL)
    {
        os_free(s_bi.file_path);
        s_bi.file_path = NULL;
    }
    if (s_bi.stop_sem != NULL)
    {
        rtos_deinit_semaphore(&s_bi.stop_sem);
        s_bi.stop_sem = NULL;
    }

    BOOT_IMAGE_LOGI("%s: boot image finished, result=%d\n", __func__, result);
}

/* ------------------------------------------------------------- worker task */

static void boot_image_task(void *arg)
{
    (void)arg;
    const char *path = s_bi.file_path;
    bk_err_t result = BK_FAIL;
    uint8_t *stream = NULL;
    boot_image_decoded_t decoded;
    os_memset(&decoded, 0, sizeof(decoded));

    do
    {
        /* 0. mount the backing filesystem (deferred here so the main init path
         *    is never blocked). */
        if (s_bi.fs_valid)
        {
            /* Workaround for an SD-card power/routing hardware limitation on this
             * board: on a cold boot the always-powered card can fail to enumerate
             * unless it is force-discharged first. No-op for non-SD filesystems or
             * an already-mounted card. See boot_image_fs_sd_cold_boot_reset(). */
            boot_image_fs_sd_cold_boot_reset(s_bi.fs);

            bool was_mounted = boot_image_fs_is_mounted(s_bi.fs);
            bk_err_t mret = BK_FAIL;
            for (uint32_t attempt = 0; attempt < BOOT_IMAGE_FS_MOUNT_RETRY; attempt++)
            {
                if (s_bi.stop_req)
                {
                    break;
                }
                mret = boot_image_fs_mount(s_bi.fs);
                if (mret == BK_OK)
                {
                    break;
                }
                BOOT_IMAGE_LOGW("%s: mount %s failed (attempt %u/%u), retry in %ums\n",
                                __func__, path, (unsigned)(attempt + 1),
                                (unsigned)BOOT_IMAGE_FS_MOUNT_RETRY,
                                (unsigned)BOOT_IMAGE_FS_MOUNT_RETRY_DELAY_MS);
                rtos_delay_milliseconds(BOOT_IMAGE_FS_MOUNT_RETRY_DELAY_MS);
            }
            if (mret != BK_OK)
            {
                BOOT_IMAGE_LOGE("%s: mount fs failed for %s after %u attempts\n",
                                __func__, path, (unsigned)BOOT_IMAGE_FS_MOUNT_RETRY);
                break;
            }
            boot_image_fs_mark_auto_mounted(s_bi.fs, !was_mounted);
        }

        if (s_bi.stop_req)
        {
            break;
        }

        /* 1. read the whole image file into a DMA-accessible buffer */
        uint32_t stream_len = 0;
        stream = boot_image_read_file(path, &stream_len);
        if (stream == NULL)
        {
            break;
        }

        /* 2. resolve the concrete format (AUTO -> detect by header/extension) */
        boot_image_format_t fmt = s_bi.cfg.format;
        if (fmt == BOOT_IMAGE_FORMAT_AUTO)
        {
            fmt = boot_image_detect_format(path, stream, stream_len);
        }
        if (fmt == BOOT_IMAGE_FORMAT_AUTO)
        {
            BOOT_IMAGE_LOGW("%s: unsupported / unrecognized image format: %s\n", __func__, path);
            break;
        }

        /* 3. decode to a native-size NV12 frame */
        if (boot_image_decode(stream, stream_len, fmt, &decoded) != AVDK_ERR_OK)
        {
            break;
        }

        /* stream is only needed for decode; free it now. */
        bk_frame_buffer_free(stream);
        stream = NULL;

        if (s_bi.stop_req)
        {
            break;
        }

        /* 4. enforce "image must match panel": the boot image is authored at the
         *    panel's native resolution (no rotation/scaling is performed). */
        if (s_bi.cfg.panel_width != 0U && s_bi.cfg.panel_height != 0U &&
            (decoded.width != s_bi.cfg.panel_width || decoded.height != s_bi.cfg.panel_height))
        {
            BOOT_IMAGE_LOGE("%s: image %ux%u != panel %ux%u; author the boot image at panel size\n",
                            __func__, (unsigned)decoded.width, (unsigned)decoded.height,
                            (unsigned)s_bi.cfg.panel_width, (unsigned)s_bi.cfg.panel_height);
            break;
        }

        /* 5. turn LCD on via the injected ops (single display/power owner). */
        if (s_bi.cfg.display_ops->lcd_open(s_bi.cfg.display_ops->user, &s_bi.lcd_handle) != BK_OK ||
            s_bi.lcd_handle == NULL)
        {
            BOOT_IMAGE_LOGE("%s: LCD open failed\n", __func__);
            s_bi.lcd_handle = NULL;
            break;
        }

        /* 6. flush the raw NV12 frame once. Consumes decoded.nv12. */
        if (boot_image_display_show(s_bi.lcd_handle, &decoded) != AVDK_ERR_OK)
        {
            BOOT_IMAGE_LOGE("%s: display show failed\n", __func__);
            break;
        }

        BOOT_IMAGE_LOGI("%s: showing %s (fmt=%d, %ux%u, hold=%ums)\n",
                        __func__, path, (int)fmt, (unsigned)decoded.width,
                        (unsigned)decoded.height, (unsigned)s_bi.cfg.display_duration_ms);

        result = BK_OK;

        /* 7. hold: for a fixed duration, or until boot_image_stop() (duration 0) */
        const uint32_t hold = s_bi.cfg.display_duration_ms;
        const uint32_t wait_ms = (hold == BOOT_IMAGE_HOLD_FOREVER) ? BEKEN_WAIT_FOREVER : hold;
        if (s_bi.stop_sem != NULL)
        {
            (void)rtos_get_semaphore(&s_bi.stop_sem, wait_ms);
        }
    } while (0);

    /* On any early break, the decoded frame may still be owned here. */
    boot_image_decoded_free(&decoded);
    if (stream != NULL)
    {
        bk_frame_buffer_free(stream);
        stream = NULL;
    }

    /* Publish the IDLE state (showing=false) as the very last write to s_bi, and
     * do it under s_bi_lock together with the resource teardown. This closes the
     * race where a concurrent boot_image_show() saw showing==false and memset()
     * s_bi while this worker was still touching thread / stop_sem. done_cb is
     * captured under the lock and fired only after IDLE is published, so a
     * re-entrant show()/stop() from the callback is safe. Once the lock is
     * released the sole remaining action is rtos_delete_thread(NULL), which
     * operates on the current TCB only and never dereferences s_bi. */
    rtos_lock_mutex(&s_bi_lock);
    boot_image_teardown(result);
    boot_image_done_cb_t done_cb = s_bi.cfg.done_cb;
    void *user_data = s_bi.cfg.user_data;
    s_bi.thread = NULL;
    s_bi.showing = false;
    rtos_unlock_mutex(&s_bi_lock);

    if (done_cb != NULL)
    {
        done_cb(result, user_data);
    }

    rtos_delete_thread(NULL);
}

/* ------------------------------------------------------------- public API */

bk_err_t boot_image_show(const boot_image_play_cfg_t *cfg)
{
    if (cfg == NULL || cfg->file_path == NULL || cfg->file_path[0] == '\0' ||
        cfg->display_ops == NULL || cfg->display_ops->lcd_open == NULL)
    {
        return BK_ERR_PARAM;
    }
    if (boot_image_lock_init() != AVDK_ERR_OK)
    {
        return BK_FAIL;
    }

    rtos_lock_mutex(&s_bi_lock);
    if (s_bi.showing)
    {
        rtos_unlock_mutex(&s_bi_lock);
        BOOT_IMAGE_LOGW("%s: already showing\n", __func__);
        return BK_ERR_BUSY;
    }

    os_memset(&s_bi, 0, sizeof(s_bi));
    s_bi.cfg = *cfg;

    size_t len = os_strlen(cfg->file_path);
    s_bi.file_path = (char *)os_malloc(len + 1);
    if (s_bi.file_path == NULL)
    {
        rtos_unlock_mutex(&s_bi_lock);
        return BK_ERR_NO_MEM;
    }
    os_memcpy(s_bi.file_path, cfg->file_path, len + 1);
    s_bi.cfg.file_path = s_bi.file_path;

    /* Resolve the backing filesystem by path prefix now, defer mount to worker. */
    if (boot_image_fs_from_path(s_bi.file_path, &s_bi.fs) == BK_OK)
    {
        s_bi.fs_valid = true;
    }
    else
    {
        BOOT_IMAGE_LOGW("%s: path prefix not recognized, assuming already mounted: %s\n",
                        __func__, s_bi.file_path);
        s_bi.fs_valid = false;
    }

    if (rtos_init_semaphore(&s_bi.stop_sem, 1) != BK_OK)
    {
        os_free(s_bi.file_path);
        s_bi.file_path = NULL;
        rtos_unlock_mutex(&s_bi_lock);
        return BK_FAIL;
    }

    s_bi.stop_req = false;
    s_bi.showing = true;

    if (rtos_create_thread(&s_bi.thread, CONFIG_BOOT_IMAGE_TASK_PRIORITY, "boot_image",
                           (beken_thread_function_t)boot_image_task,
                           CONFIG_BOOT_IMAGE_TASK_STACK_SIZE, NULL) != BK_OK)
    {
        BOOT_IMAGE_LOGE("%s: create thread failed\n", __func__);
        s_bi.showing = false;
        rtos_deinit_semaphore(&s_bi.stop_sem);
        s_bi.stop_sem = NULL;
        os_free(s_bi.file_path);
        s_bi.file_path = NULL;
        rtos_unlock_mutex(&s_bi_lock);
        return BK_FAIL;
    }

    rtos_unlock_mutex(&s_bi_lock);
    return BK_OK;
}

bk_err_t boot_image_stop(void)
{
    /* Lock is created lazily by boot_image_show(); if it does not exist yet
     * nothing has ever been shown, so there is nothing to stop. */
    if (s_bi_lock == NULL)
    {
        return BK_OK;
    }

    /* Hold the lock across the whole check-and-signal. The worker frees
     * stop_sem inside boot_image_teardown() under this same lock, so we can
     * never post to an already-freed semaphore (fixes the stop_sem TOCTOU/UAF). */
    rtos_lock_mutex(&s_bi_lock);
    if (s_bi.showing)
    {
        s_bi.stop_req = true;
        if (s_bi.stop_sem != NULL)
        {
            rtos_set_semaphore(&s_bi.stop_sem);
        }
    }
    rtos_unlock_mutex(&s_bi_lock);
    return BK_OK;
}

bool boot_image_is_showing(void)
{
    return s_bi.showing;
}
