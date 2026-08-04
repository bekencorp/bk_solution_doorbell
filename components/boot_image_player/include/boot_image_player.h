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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <common/bk_err.h>
#include <components/bk_display.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Boot image (splash) display component.
 *
 * Shows a single still boot image once during power-up:
 *   - The file can live on the SD card (/sd0) or internal flash (/if0); the
 *     component mounts the filesystem on demand based on the path prefix.
 *   - The image is read from VFS, decoded (JPEG for now; the decode path is
 *     dispatched by format so PNG/BMP can be added later) and flushed to the
 *     LCD once as raw NV12. No rotation/scaling is performed: the boot image
 *     MUST be authored at the panel's native resolution and orientation.
 *   - Lifecycle is configurable via display_duration_ms:
 *       > 0  : keep the image on screen for that long, then tear down.
 *       == 0 : keep it on screen until boot_image_stop() is called (splash that
 *              is handed over to the business UI).
 *   - The component does not know how to bring up the panel itself; the LCD
 *     power/handle is provided through boot_image_display_ops_t so the component
 *     stays decoupled from any board/app display service. The same single owner
 *     that runs the business display must back these callbacks.
 *   - Display runs on its own thread; boot_image_show() returns immediately.
 *
 * This component is independent of boot_video_player; the two are mutually
 * exclusive at boot (the upper layer picks one).
 */

/** Filesystem backing the boot image file. */
typedef enum
{
    BOOT_IMAGE_FS_SD = 0,            /**< SD card, mounted at /sd0 (FATFS). */
    BOOT_IMAGE_FS_INTERNAL_FLASH,    /**< Internal flash, mounted at /if0 (FATFS). */
    BOOT_IMAGE_FS_MAX,
} boot_image_fs_t;

/** Image container format. */
typedef enum
{
    BOOT_IMAGE_FORMAT_AUTO = 0,      /**< Infer from file extension / magic (JPEG only for now). */
    BOOT_IMAGE_FORMAT_JPEG,          /**< Baseline JPEG. */
    /* Reserved for future work (not implemented):
     * BOOT_IMAGE_FORMAT_PNG,
     * BOOT_IMAGE_FORMAT_BMP, */
} boot_image_format_t;

/** Display power ownership during / after display. */
typedef enum
{
    BOOT_IMAGE_DISPLAY_ON_OFF = 0,   /**< Turn LCD on (ops.open), turn it off on teardown (ops.close). Default. */
    BOOT_IMAGE_DISPLAY_KEEP_ON,      /**< Turn LCD on (ops.open), keep it on after teardown (ops.close NOT called). */
    BOOT_IMAGE_DISPLAY_ASSUME_ON,    /**< ops.open is expected to be a no-op turn-on (already on); ops.close NOT called. */
} boot_image_display_mode_t;

/** display_duration_ms value meaning "hold until boot_image_stop()". */
#define BOOT_IMAGE_HOLD_FOREVER  (0u)

/**
 * @brief Display done/failed notification (invoked from the display thread).
 * @param result BK_OK when the image was shown (and the hold elapsed / was
 *               stopped), error code otherwise.
 * @param user_data Value copied from boot_image_play_cfg_t::user_data.
 */
typedef void (*boot_image_done_cb_t)(bk_err_t result, void *user_data);

/**
 * @brief Injected LCD bring-up interface (dependency inversion).
 *
 * The component owns the pixel path (it sets the DPU to raw NV12 and flushes
 * via bk_display directly), but delegates the board/app-specific panel power +
 * handle acquisition to the caller so it does not depend on any display
 * service. The backing implementation MUST be the single owner of the display
 * power domain (e.g. it forwards to the same app display service the business
 * UI uses), so the boot splash and the business display never double-own the
 * panel / power vote.
 */
typedef struct
{
    /** Turn the LCD on (idempotent) and return the bk_display controller handle. */
    bk_err_t (*lcd_open)(void *user, bk_display_ctlr_handle_t *out_handle);
    /** Turn the LCD off. Only invoked for BOOT_IMAGE_DISPLAY_ON_OFF. */
    bk_err_t (*lcd_close)(void *user);
    void      *user;                               /**< Opaque, passed back to lcd_open/lcd_close. */
} boot_image_display_ops_t;

/** Boot image display configuration. */
typedef struct
{
    const char                     *file_path;           /**< Full VFS path, e.g. "/sd0/boot.jpg". */
    boot_image_format_t             format;              /**< BOOT_IMAGE_FORMAT_AUTO or a specific format. */
    uint32_t                        display_duration_ms; /**< > 0: auto teardown after; 0: hold until stop. */
    boot_image_display_mode_t       display_mode;        /**< Default BOOT_IMAGE_DISPLAY_ON_OFF. */
    const boot_image_display_ops_t *display_ops;         /**< LCD bring-up ops (required); must outlive the show. */
    uint16_t                        panel_width;         /**< Panel width; decoded image MUST equal it (0 = skip check). */
    uint16_t                        panel_height;        /**< Panel height; decoded image MUST equal it (0 = skip check). */
    boot_image_done_cb_t            done_cb;             /**< Optional completion callback. */
    void                           *user_data;           /**< Passed back to done_cb. */
} boot_image_play_cfg_t;

/**
 * @brief Mount the filesystem backing boot image files (idempotent).
 * @param fs BOOT_IMAGE_FS_SD or BOOT_IMAGE_FS_INTERNAL_FLASH.
 * @return BK_OK on success (or already mounted), error code otherwise.
 */
bk_err_t boot_image_fs_mount(boot_image_fs_t fs);

/**
 * @brief Unmount a filesystem previously mounted by this component.
 * @param fs BOOT_IMAGE_FS_SD or BOOT_IMAGE_FS_INTERNAL_FLASH.
 * @return BK_OK on success, error code otherwise.
 */
bk_err_t boot_image_fs_unmount(boot_image_fs_t fs);

/**
 * @brief Query whether a filesystem is currently mounted by this component.
 */
bool boot_image_fs_is_mounted(boot_image_fs_t fs);

/**
 * @brief Start boot image display asynchronously.
 *
 * Returns immediately. The filesystem for cfg->file_path is auto-mounted (by
 * path prefix) if needed, then a worker thread reads and decodes the image,
 * turns on the LCD (cfg->display_ops->lcd_open), flushes the raw NV12 frame,
 * holds it (per display_duration_ms), and cleans up (turning the LCD off via
 * cfg->display_ops->lcd_close for BOOT_IMAGE_DISPLAY_ON_OFF).
 *
 * @param cfg Display configuration. file_path must be a valid VFS path and
 *            display_ops (with a non-NULL lcd_open) must be provided.
 * @return BK_OK if the worker was started, BK_ERR_BUSY if already showing,
 *         BK_ERR_PARAM on invalid arguments.
 */
bk_err_t boot_image_show(const boot_image_play_cfg_t *cfg);

/**
 * @brief Request the running boot image display to stop and tear down.
 *
 * For BOOT_IMAGE_HOLD_FOREVER this is how the upper layer hands the display
 * back. Idempotent; returns BK_OK when nothing is showing. Thread-safe: the
 * request is serialized against the worker's teardown under an internal lock,
 * so it is safe to call concurrently with the worker finishing on its own.
 * @return BK_OK on success.
 */
bk_err_t boot_image_stop(void);

/**
 * @brief Query whether a boot image is currently showing.
 *
 * Best-effort snapshot for logging/diagnostics. Do NOT poll this and then call
 * boot_image_show(): rely on boot_image_show()'s own BK_ERR_BUSY return, which
 * performs the check-and-start atomically.
 */
bool boot_image_is_showing(void);

#ifdef __cplusplus
}
#endif
