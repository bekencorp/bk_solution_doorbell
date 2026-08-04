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
 * Display path for the boot image player.
 *
 * The boot image is authored at the panel's native resolution/orientation, so
 * the component performs no rotation or scaling: it switches the DPU to raw
 * NV12 and flushes the decoded frame once. Panel bring-up (power, DSI, backlight
 * and the controller handle) is owned by the caller and reaches this file only
 * as an already-turned-on bk_display controller handle, keeping the component
 * decoupled from any board/app display service.
 */

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include <components/bk_display.h>
#include <components/bk_frame_buffer.h>
#include <common/avdk_pixel_types.h>

#include "boot_image_priv.h"

/* ======================================================================== *
 *  DPU runtime pixel-format (raw NV12)
 * ======================================================================== */

/* The panel is brought up by the business layer with a format tailored to the
 * live video path (compressed ARGB8888). The boot image is raw NV12, so switch
 * the DPU runtime format to raw NV12 before the (single) flush. The boot image
 * flushes exactly one frame per show, so there is no repeated switch to cache. */
static avdk_err_t boot_image_lcd_apply_nv12_format(bk_display_ctlr_handle_t handle)
{
    if (handle == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    bk_display_pixel_format_config_t cfg;
    os_memset(&cfg, 0, sizeof(cfg));
    cfg.format = BK_PIXEL_FORMAT_NV12;
    cfg.decompress = false;

    avdk_err_t ret = bk_display_pixel_format_set(handle, &cfg);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_IMAGE_LOGE("%s: DPU runtime format switch failed, ret=%d\n", __func__, ret);
    }
    return ret;
}

/* ======================================================================== *
 *  Single-frame show
 * ======================================================================== */

static avdk_err_t boot_image_frame_free_cb(void *frame)
{
    bk_frame_buffer_free(frame);
    return AVDK_ERR_OK;
}

avdk_err_t boot_image_display_show(bk_display_ctlr_handle_t handle,
                                   boot_image_decoded_t *decoded)
{
    if (handle == NULL || decoded == NULL || decoded->nv12 == NULL)
    {
        if (decoded != NULL)
        {
            boot_image_decoded_free(decoded);
        }
        return AVDK_ERR_INVAL;
    }

    avdk_err_t ret = boot_image_lcd_apply_nv12_format(handle);
    if (ret != AVDK_ERR_OK)
    {
        boot_image_decoded_free(decoded);
        return ret;
    }

    void *pixel = decoded->nv12;
    decoded->nv12 = NULL; /* ownership transferred to the flush below */

    ret = bk_display_flush(handle, pixel, boot_image_frame_free_cb);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_IMAGE_LOGE("%s: bk_display_flush failed, ret=%d\n", __func__, ret);
        bk_frame_buffer_free(pixel);
        return ret;
    }

    return AVDK_ERR_OK;
}
