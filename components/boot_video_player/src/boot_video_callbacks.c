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
 * Engine buffer (de)allocation callbacks.
 *
 * Ported from projects/multimedia/video_player_example (video_play_callbacks.c),
 * keeping only the boot-video code paths:
 *   - encoded video packets  -> CODED slab (PSRAM)
 *   - decoded video frames    -> UNCODED slab (PSRAM), allocator-aware free
 *   - audio packets/frames    -> plain heap with a small safety pad
 */

#include <common/bk_include.h>
#include <os/mem.h>

#include <components/bk_frame_buffer.h>
#include <components/bk_video_player/bk_video_player_types.h>
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
#include <components/bk_video_player/video_decoder/bk_video_player_hw_h264_decoder.h>
#endif

#include "boot_video_priv.h"

#define BOOT_VIDEO_PACKET_BUFFER_SAFETY_PAD_BYTES   (2048U)
#define BOOT_VIDEO_FRAME_BUFFER_SAFETY_PAD_BYTES    (128U)

/* mem_slab debug guards require user_size 64 B-aligned (see bk_mem_slab.c). */
#define BOOT_VIDEO_MEM_SLAB_ALIGN_BYTES             (64U)

static inline uint32_t boot_video_slab_alloc_size(uint32_t payload_plus_pad)
{
    return (payload_plus_pad + BOOT_VIDEO_MEM_SLAB_ALIGN_BYTES - 1U) &
           ~(BOOT_VIDEO_MEM_SLAB_ALIGN_BYTES - 1U);
}

/* ---------------- audio packet/frame buffers (plain heap) ---------------- */

avdk_err_t boot_video_audio_buffer_alloc_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0)
    {
        return AVDK_ERR_INVAL;
    }

    const uint32_t requested = buffer->length;
    buffer->data = (uint8_t *)os_malloc(requested + BOOT_VIDEO_PACKET_BUFFER_SAFETY_PAD_BYTES);
    if (buffer->data == NULL)
    {
        buffer->length = 0;
        return AVDK_ERR_NOMEM;
    }

    buffer->frame_buffer = NULL;
    buffer->length = requested;
    buffer->user_data = NULL;
    return AVDK_ERR_OK;
}

void boot_video_audio_buffer_free_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL)
    {
        return;
    }

    if (buffer->data != NULL)
    {
        os_free(buffer->data);
        buffer->data = NULL;
    }

    buffer->length = 0;
    buffer->pts = 0;
    buffer->frame_buffer = NULL;
    buffer->user_data = NULL;
}

/* --------------- encoded video packet buffers (CODED slab) --------------- */

avdk_err_t boot_video_video_packet_buffer_alloc_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0)
    {
        return AVDK_ERR_INVAL;
    }

    const uint32_t requested = buffer->length;
    const uint32_t alloc_size = boot_video_slab_alloc_size(
        requested + BOOT_VIDEO_PACKET_BUFFER_SAFETY_PAD_BYTES);

    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, alloc_size);
    if (frame == NULL)
    {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0;
        return AVDK_ERR_NOMEM;
    }

    buffer->data = frame;
    buffer->frame_buffer = frame;
    buffer->length = requested;
    buffer->user_data = NULL;
    return AVDK_ERR_OK;
}

void boot_video_video_packet_buffer_free_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL)
    {
        return;
    }

    if (buffer->frame_buffer != NULL)
    {
        bk_frame_buffer_free(buffer->frame_buffer);
        buffer->frame_buffer = NULL;
    }

    buffer->data = NULL;
    buffer->length = 0;
    buffer->pts = 0;
    buffer->user_data = NULL;
}

/* ---------------- decoded video frame buffers (UNCODED slab) ------------- */

avdk_err_t boot_video_video_buffer_alloc_yuv_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0)
    {
        return AVDK_ERR_INVAL;
    }

    const uint32_t requested = buffer->length;
    const uint32_t alloc_size = boot_video_slab_alloc_size(
        requested + BOOT_VIDEO_FRAME_BUFFER_SAFETY_PAD_BYTES);

    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, alloc_size);
    if (frame == NULL)
    {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0;
        return AVDK_ERR_NOMEM;
    }

    buffer->data         = frame;
    buffer->frame_buffer = NULL;
    buffer->length       = requested;
    buffer->user_data    = NULL;
    return AVDK_ERR_OK;
}

void boot_video_video_buffer_free_yuv_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL)
    {
        return;
    }

    if (buffer->data != NULL)
    {
#if CONFIG_BK_VIDEO_PLAYER_ENABLE_HW_H264_VIDEO_DECODER
        (void)bk_video_player_hw_h264_decoder_free_output_frame(buffer->data);
#else
        bk_frame_buffer_free(buffer->data);
#endif
    }

    buffer->data         = NULL;
    buffer->frame_buffer = NULL;
    buffer->length       = 0;
    buffer->pts          = 0;
    buffer->user_data    = NULL;
}

/* ------- decoded video frame buffers, CODED slab / PSRAM1 (H264 frame-zc) ---
 *
 * The H264 frame-zerocopy decoder owns an internal fbpool in PSRAM0 (UNCODED),
 * so its displayable NV12 output frames are placed in PSRAM1 (CODED) to keep
 * the PSRAM0 budget free. Both PSRAM windows are non-cacheable, so the DPU
 * reads them coherently. Unlike the Flexa+GPU path, these frames are plain
 * bk_frame_buffer allocations, freed with bk_frame_buffer_free().
 */
avdk_err_t boot_video_video_buffer_alloc_yuv_coded_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL || buffer->length == 0)
    {
        return AVDK_ERR_INVAL;
    }

    const uint32_t requested = buffer->length;
    const uint32_t alloc_size = boot_video_slab_alloc_size(
        requested + BOOT_VIDEO_FRAME_BUFFER_SAFETY_PAD_BYTES);

    void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, alloc_size);
    if (frame == NULL)
    {
        buffer->data = NULL;
        buffer->frame_buffer = NULL;
        buffer->length = 0;
        return AVDK_ERR_NOMEM;
    }

    buffer->data         = frame;
    buffer->frame_buffer = NULL;
    buffer->length       = requested;
    buffer->user_data    = NULL;
    return AVDK_ERR_OK;
}

void boot_video_video_buffer_free_yuv_coded_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;

    if (buffer == NULL)
    {
        return;
    }

    if (buffer->data != NULL)
    {
        bk_frame_buffer_free(buffer->data);
    }

    buffer->data         = NULL;
    buffer->frame_buffer = NULL;
    buffer->length       = 0;
    buffer->pts          = 0;
    buffer->user_data    = NULL;
}
