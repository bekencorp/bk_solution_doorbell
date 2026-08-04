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
#include <common/avdk_pixel_types.h>

#include "boot_image_player.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_IMAGE_TAG "boot_image"

#define BOOT_IMAGE_LOGI(...) BK_LOGI(BOOT_IMAGE_TAG, ##__VA_ARGS__)
#define BOOT_IMAGE_LOGW(...) BK_LOGW(BOOT_IMAGE_TAG, ##__VA_ARGS__)
#define BOOT_IMAGE_LOGE(...) BK_LOGE(BOOT_IMAGE_TAG, ##__VA_ARGS__)
#define BOOT_IMAGE_LOGD(...) BK_LOGD(BOOT_IMAGE_TAG, ##__VA_ARGS__)

/* Decoded still image (NV12), owned by the caller until handed to display. */
typedef struct
{
    void    *nv12;        /* NV12 buffer (bk_frame_buffer_malloc UNCODED). */
    uint32_t size;        /* Allocated size of nv12. */
    uint16_t width;       /* Decoded (output) width. */
    uint16_t height;      /* Decoded (output) height. */
    uint16_t src_width;   /* Original image width (for rotation decisions). */
    uint16_t src_height;  /* Original image height. */
} boot_image_decoded_t;

/* ===================== decoder (boot_image_decoder.c) ==================== */

/* Detect the image format from the file path extension and/or header bytes.
 * Returns BOOT_IMAGE_FORMAT_JPEG when recognized, or the unchanged AUTO value
 * (0) when not recognized (caller treats that as unsupported). */
boot_image_format_t boot_image_detect_format(const char *path,
                                             const uint8_t *hdr, uint32_t hdr_len);

/*
 * Decode a still image bitstream to an NV12 frame.
 * @param stream/len  Input encoded bitstream (must be in a DMA-accessible heap;
 *                    the caller uses bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED)).
 * @param fmt         Concrete format (JPEG). AUTO is rejected here.
 * @param out         Receives the decoded NV12 frame on success.
 * Native-size decode: out->width/height == the image's own dimensions.
 */
avdk_err_t boot_image_decode(const uint8_t *stream, uint32_t len,
                             boot_image_format_t fmt,
                             boot_image_decoded_t *out);
void boot_image_decoded_free(boot_image_decoded_t *out);

/* ===================== display (boot_image_display.c) ==================== */

/*
 * Show one decoded NV12 frame on the given (already turned-on) controller.
 * Switches the DPU to raw NV12 and flushes the frame once. Takes ownership of
 * decoded->nv12: on success the frame is handed to the DPU via bk_display_flush
 * and freed by the flush free-callback when the LCD is closed / replaced; on
 * failure the buffer is freed here. In all cases decoded->nv12 is consumed.
 */
avdk_err_t boot_image_display_show(bk_display_ctlr_handle_t handle,
                                   boot_image_decoded_t *decoded);

#ifdef __cplusplus
}
#endif
