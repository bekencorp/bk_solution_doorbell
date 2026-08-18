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

#include <components/avdk_utils/avdk_error.h>
#include <components/bk_display.h>
#include <components/bk_video_player/bk_video_player_types.h>

#include "boot_video_player.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_VIDEO_TAG "boot_video"

#define BOOT_VIDEO_LOGI(...) BK_LOGI(BOOT_VIDEO_TAG, ##__VA_ARGS__)
#define BOOT_VIDEO_LOGW(...) BK_LOGW(BOOT_VIDEO_TAG, ##__VA_ARGS__)
#define BOOT_VIDEO_LOGE(...) BK_LOGE(BOOT_VIDEO_TAG, ##__VA_ARGS__)
#define BOOT_VIDEO_LOGD(...) BK_LOGD(BOOT_VIDEO_TAG, ##__VA_ARGS__)

/* Opaque audio output handle (implemented in boot_video_audio.c). */
typedef void *boot_video_audio_handle_t;

/* Runtime LCD/video pixel-format the DPU layer must be switched to. */
typedef enum
{
    BOOT_VIDEO_LCD_FMT_NV12_RAW = 0,
    BOOT_VIDEO_LCD_FMT_RGB565_RAW,
    BOOT_VIDEO_LCD_FMT_RGB888_RAW,
    BOOT_VIDEO_LCD_FMT_ARGB8888_RAW,
    BOOT_VIDEO_LCD_FMT_ARGB8888_COMPRESSED,
} boot_video_lcd_fmt_t;

/* Display rotation requested for the decoded frames. */
typedef enum
{
    BOOT_VIDEO_ROTATE_NONE = 0,
    BOOT_VIDEO_ROTATE_90,
    BOOT_VIDEO_ROTATE_270,
} boot_video_rotate_mode_t;

/*
 * Shared runtime context handed to the engine via bk_video_player_config_t.user_data.
 * Read by the video decode-complete callback (lcd_handle) and audio decode-complete
 * callback (audio_player_handle).
 */
typedef struct
{
    bk_display_ctlr_handle_t   lcd_handle;
    boot_video_audio_handle_t  audio_player_handle;
    uint8_t                    audio_volume; /* 0-100 */
    bool                       audio_muted;
} boot_video_ctx_t;

/* ===================== display (boot_video_display.c) ===================== */

avdk_err_t boot_video_lcd_open_with_format(const boot_video_display_ops_t *ops,
                                           bk_display_ctlr_handle_t *out_handle,
                                           boot_video_lcd_fmt_t fmt);
avdk_err_t boot_video_lcd_close(const boot_video_display_ops_t *ops);
avdk_err_t boot_video_lcd_apply_format(bk_display_ctlr_handle_t handle, boot_video_lcd_fmt_t fmt);
boot_video_lcd_fmt_t boot_video_lcd_format_for_video_codec(video_player_video_format_t format);

void boot_video_video_set_rotate_mode(boot_video_rotate_mode_t mode);
uint32_t boot_video_video_get_rotate_degree(void);

avdk_err_t boot_video_display_worker_init(void);
void boot_video_display_worker_deinit(void);
void boot_video_lcd_runtime_format_reset(void);

/* Engine video decode-complete callback (display path). */
void boot_video_video_decode_complete_cb(void *user_data,
                                         const video_player_video_frame_meta_t *meta,
                                         video_player_buffer_t *buffer);

/* ===================== buffers (boot_video_callbacks.c) =================== */

avdk_err_t boot_video_video_packet_buffer_alloc_cb(void *user_data, video_player_buffer_t *buffer);
void boot_video_video_packet_buffer_free_cb(void *user_data, video_player_buffer_t *buffer);
avdk_err_t boot_video_video_buffer_alloc_yuv_cb(void *user_data, video_player_buffer_t *buffer);
void boot_video_video_buffer_free_yuv_cb(void *user_data, video_player_buffer_t *buffer);
avdk_err_t boot_video_video_buffer_alloc_yuv_coded_cb(void *user_data, video_player_buffer_t *buffer);
void boot_video_video_buffer_free_yuv_coded_cb(void *user_data, video_player_buffer_t *buffer);
avdk_err_t boot_video_audio_buffer_alloc_cb(void *user_data, video_player_buffer_t *buffer);
void boot_video_audio_buffer_free_cb(void *user_data, video_player_buffer_t *buffer);

/* ===================== audio (boot_video_audio.c) ======================== */

/*
 * Open the onboard-speaker audio output for the probed media_info.
 * Returns AVDK_ERR_OK and *out_handle != NULL on success. On any failure the
 * caller should continue playback without audio.
 */
avdk_err_t boot_video_audio_open(const video_player_media_info_t *media_info,
                                 uint8_t volume,
                                 bool mute,
                                 boot_video_audio_handle_t *out_handle);
void boot_video_audio_close(boot_video_audio_handle_t handle);

/* Engine audio callbacks. */
void boot_video_audio_decode_complete_cb(void *user_data,
                                         const video_player_audio_packet_meta_t *meta,
                                         video_player_buffer_t *buffer);
avdk_err_t boot_video_audio_set_volume_cb(void *user_data, uint8_t volume);
avdk_err_t boot_video_audio_set_mute_cb(void *user_data, bool mute);
avdk_err_t boot_video_audio_output_config_cb(void *user_data, const video_player_audio_params_t *params);

#ifdef __cplusplus
}
#endif
