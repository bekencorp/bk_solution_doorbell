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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Boot video player component.
 *
 * Plays a boot animation video (MP4/AVI, MJPEG/H.264 video + optional AAC audio)
 * once during power-up:
 *   - The file can live on the SD card (/sd0) or internal flash (/if0); the
 *     component mounts the filesystem on demand based on the path prefix.
 *   - Media info is probed first, then only the required container parser /
 *     video decoder / audio decoder are registered. Audio output is opened
 *     only when the file actually contains an audio track.
 *   - The component turns the LCD on for playback and (by default) turns it off
 *     afterwards, leaving the business layer to turn it back on when ready.
 *   - Playback runs on its own thread; boot_video_play() returns immediately.
 */

/** Filesystem backing the boot video file. */
typedef enum
{
    BOOT_VIDEO_FS_SD = 0,            /**< SD card, mounted at /sd0 (FATFS). */
    BOOT_VIDEO_FS_INTERNAL_FLASH,    /**< Internal flash, mounted at /if0 (FATFS). */
    BOOT_VIDEO_FS_MAX,
} boot_video_fs_t;

/** Display power ownership during / after playback. */
typedef enum
{
    BOOT_VIDEO_DISPLAY_ON_OFF = 0,   /**< Component turns LCD on, turns it off when done (default). */
    BOOT_VIDEO_DISPLAY_KEEP_ON,      /**< Component turns LCD on, keeps it on when done. */
    BOOT_VIDEO_DISPLAY_ASSUME_ON,    /**< Assume upper layer already turned the LCD on; never touch power. */
} boot_video_display_mode_t;

/** Let the component pick rotation from panel vs. video orientation. */
#define BOOT_VIDEO_ROTATE_AUTO   (0xFFFFFFFFu)

/**
 * @brief Playback done/failed notification (invoked from the playback thread).
 * @param result BK_OK when playback finished normally, error code otherwise.
 * @param user_data Value copied from boot_video_play_cfg_t::user_data.
 */
typedef void (*boot_video_done_cb_t)(bk_err_t result, void *user_data);

/** Boot video playback configuration. */
typedef struct
{
    const char               *file_path;     /**< Full VFS path, e.g. "/sd0/boot.mp4" or "/if0/boot.avi". */
    uint32_t                  rotate_degree; /**< 0/90/180/270 or BOOT_VIDEO_ROTATE_AUTO. */
    uint8_t                   volume;        /**< 0-100, only used when the file has audio. */
    bool                      mute;          /**< Only used when the file has audio. */
    boot_video_display_mode_t display_mode;  /**< Default BOOT_VIDEO_DISPLAY_ON_OFF. */
    boot_video_done_cb_t      done_cb;       /**< Optional completion callback. */
    void                     *user_data;     /**< Passed back to done_cb. */
} boot_video_play_cfg_t;

/**
 * @brief Mount the filesystem backing boot video files (idempotent).
 * @param fs BOOT_VIDEO_FS_SD or BOOT_VIDEO_FS_INTERNAL_FLASH.
 * @return BK_OK on success (or already mounted), error code otherwise.
 */
bk_err_t boot_video_fs_mount(boot_video_fs_t fs);

/**
 * @brief Unmount a filesystem previously mounted by this component.
 * @param fs BOOT_VIDEO_FS_SD or BOOT_VIDEO_FS_INTERNAL_FLASH.
 * @return BK_OK on success, error code otherwise.
 */
bk_err_t boot_video_fs_unmount(boot_video_fs_t fs);

/**
 * @brief Query whether a filesystem is currently mounted by this component.
 */
bool boot_video_fs_is_mounted(boot_video_fs_t fs);

/**
 * @brief Start boot video playback asynchronously.
 *
 * Returns immediately. The filesystem for cfg->file_path is auto-mounted (by
 * path prefix) if needed, then a worker thread probes media info, registers the
 * required modules, turns on the LCD, plays, and cleans up (turning the LCD off
 * for BOOT_VIDEO_DISPLAY_ON_OFF).
 *
 * @param cfg Playback configuration (file_path must be a valid VFS path).
 * @return BK_OK if the worker was started, BK_ERR_BUSY if already playing,
 *         BK_ERR_PARAM on invalid arguments.
 */
bk_err_t boot_video_play(const boot_video_play_cfg_t *cfg);

/**
 * @brief Request the running boot video playback to stop and tear down.
 * @return BK_OK on success.
 */
bk_err_t boot_video_stop(void);

/**
 * @brief Query whether a boot video is currently playing.
 */
bool boot_video_is_playing(void);

#ifdef __cplusplus
}
#endif
