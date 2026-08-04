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
 * Still-image decode for the boot image player.
 *
 * Format is dispatched (JPEG only for now); the JPEG path uses the hardware
 * frame-mode decoder from the bk_decoder component (vcdec), decoding the whole
 * image synchronously into a native-size NV12 frame. Modelled on
 * projects/multimedia/jpeg_decode_example/.../vcdec_jpeg_test.c.
 */

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include <components/bk_frame_buffer.h>
#include <common/avdk_pixel_types.h>
#include "components/bk_decode/bk_jpeg_decode_ctlr.h"

#include "boot_image_priv.h"

/* Large full-screen images can take a while; give the HW plenty of headroom. */
#define BOOT_IMAGE_JPEG_DECODE_TIMEOUT_MS   (3000U)

boot_image_format_t boot_image_detect_format(const char *path,
                                             const uint8_t *hdr, uint32_t hdr_len)
{
    /* Magic: JPEG starts with FF D8 FF. */
    if (hdr != NULL && hdr_len >= 3U &&
        hdr[0] == 0xFFU && hdr[1] == 0xD8U && hdr[2] == 0xFFU)
    {
        return BOOT_IMAGE_FORMAT_JPEG;
    }

    if (path != NULL)
    {
        size_t plen = os_strlen(path);
        static const char *jpg_exts[] = { ".jpg", ".jpeg", ".jpe" };
        for (uint32_t e = 0; e < (uint32_t)(sizeof(jpg_exts) / sizeof(jpg_exts[0])); e++)
        {
            const char *ext = jpg_exts[e];
            size_t elen = os_strlen(ext);
            if (plen < elen)
            {
                continue;
            }
            const char *p = path + (plen - elen);
            bool match = true;
            for (size_t i = 0; i < elen; i++)
            {
                char a = p[i];
                char b = ext[i];
                if (a >= 'A' && a <= 'Z')
                {
                    a = (char)(a - 'A' + 'a');
                }
                if (a != b)
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                return BOOT_IMAGE_FORMAT_JPEG;
            }
        }
    }

    return BOOT_IMAGE_FORMAT_AUTO; /* unrecognized -> caller treats as unsupported */
}

/*
 * Pack a hardware-decoded NV12 frame that was written with a 16-aligned luma
 * stride (aln_w) / macroblock-aligned luma height (aln_h) down to a tight
 * out_w-stride frame the display path can consume directly.
 *
 * The HW "direct NV12" JPEG path writes:
 *   Y  : aln_w x aln_h, luma stride = aln_w
 *   UV : starts at aln_w * aln_h, uv stride = aln_w, uv rows = aln_h / 2
 * We keep only the visible w x h luma and w x (h/2) chroma, both at stride w.
 *
 * Done in place with forward memmove: every destination row starts at or
 * before its source row, so the copies never clobber not-yet-read data.
 */
static void boot_image_nv12_pack(uint8_t *buf, uint16_t w, uint16_t h,
                                 uint16_t aln_w, uint16_t aln_h)
{
    if (buf == NULL || (aln_w == w && aln_h == h))
    {
        return;
    }

    /* Luma: h rows, aln_w stride -> w stride. */
    for (uint32_t r = 0; r < (uint32_t)h; r++)
    {
        os_memmove(buf + (uint32_t)r * w, buf + (uint32_t)r * aln_w, w);
    }

    /* Chroma (interleaved UV): h/2 rows, aln_w stride -> w stride. */
    const uint8_t *src_uv = buf + (uint32_t)aln_w * aln_h;
    uint8_t *dst_uv       = buf + (uint32_t)w * h;
    const uint32_t uv_rows = (uint32_t)h / 2U;
    for (uint32_t r = 0; r < uv_rows; r++)
    {
        os_memmove(dst_uv + (uint32_t)r * w, src_uv + (uint32_t)r * aln_w, w);
    }
}

static void boot_image_jpeg_destroy(bk_jpeg_decode_ctlr_handle_t *dec)
{
    if (dec == NULL || *dec == NULL)
    {
        return;
    }
    (void)bk_jpeg_decode_close(*dec);
    (void)bk_jpeg_decode_deinit(*dec);
    (void)bk_jpeg_decode_delete(*dec);
    *dec = NULL;
}

static avdk_err_t boot_image_decode_jpeg(const uint8_t *stream, uint32_t len,
                                         boot_image_decoded_t *out)
{
    bk_jpeg_decode_img_info_t img_info;
    os_memset(&img_info, 0, sizeof(img_info));
    img_info.input_stream = (uint8_t *)(uintptr_t)stream;
    img_info.input_stream_length = len;

    avdk_err_t ret = bk_jpeg_decode_get_img_info(&img_info);
    if (ret != AVDK_ERR_OK || img_info.width == 0U || img_info.height == 0U)
    {
        BOOT_IMAGE_LOGE("%s: get_img_info failed, ret=%d (w=%u h=%u)\n",
                        __func__, ret, (unsigned)img_info.width, (unsigned)img_info.height);
        return (ret != AVDK_ERR_OK) ? ret : AVDK_ERR_GENERIC;
    }

    /* NV12 4:2:0 requires even dimensions; round the decode output down to even. */
    const uint16_t out_w = (uint16_t)(img_info.width & ~1U);
    const uint16_t out_h = (uint16_t)(img_info.height & ~1U);
    if (out_w == 0U || out_h == 0U)
    {
        BOOT_IMAGE_LOGE("%s: degenerate image size %ux%u\n", __func__,
                        (unsigned)img_info.width, (unsigned)img_info.height);
        return AVDK_ERR_UNSUPPORTED;
    }

    /*
     * The HW "direct NV12" JPEG path pads luma to whole 16x16 macroblocks in
     * BOTH dimensions: the luma stride is the 16-aligned width and the chroma
     * plane starts after the 16-aligned luma plane. The output buffer must be
     * sized for those aligned dimensions (vcdec rejects a smaller buffer), so
     * decode into an aligned buffer and pack it down to a tight frame after.
     */
    const uint16_t aln_w = (uint16_t)((out_w + 15U) & ~15U);
    const uint16_t aln_h = (uint16_t)((out_h + 15U) & ~15U);
    const uint32_t decode_size = (uint32_t)aln_w * (uint32_t)aln_h * 3U / 2U;
    const uint32_t tight_size  = (uint32_t)out_w * (uint32_t)out_h * 3U / 2U;

    uint8_t *out_buf = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, decode_size);
    if (out_buf == NULL)
    {
        BOOT_IMAGE_LOGE("%s: alloc NV12 output failed, size=%u\n", __func__, (unsigned)decode_size);
        return AVDK_ERR_NOMEM;
    }
    os_memset(out_buf, 0, decode_size);

    bk_jpeg_decode_frame_config_t cfg = DEFAULT_JPEG_DECODE_FRAME_CONFIG;
    cfg.frame_done_cb = NULL;
    cfg.frame_done_args = NULL;
    cfg.timeout_ms = BOOT_IMAGE_JPEG_DECODE_TIMEOUT_MS;
    cfg.out_width = out_w;
    cfg.out_height = out_h;
    cfg.out_format = BK_PIXEL_FORMAT_NV12;

    bk_jpeg_decode_ctlr_handle_t dec = NULL;
    ret = bk_jpeg_decode_frame_ctlr_new(&dec, &cfg);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_IMAGE_LOGE("%s: frame_ctlr_new failed, ret=%d\n", __func__, ret);
        goto fail;
    }
    ret = bk_jpeg_decode_init(dec);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_IMAGE_LOGE("%s: decode_init failed, ret=%d\n", __func__, ret);
        goto fail;
    }
    ret = bk_jpeg_decode_open(dec);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_IMAGE_LOGE("%s: decode_open failed, ret=%d\n", __func__, ret);
        goto fail;
    }

    bk_jpeg_decode_input_t in;
    os_memset(&in, 0, sizeof(in));
    in.stream = (uint8_t *)(uintptr_t)stream;
    in.stream_len = len;
    in.out_buffer = out_buf;
    in.out_buffer_size = decode_size;
    ret = bk_jpeg_decode_frame(dec, &in);
    if (ret != AVDK_ERR_OK)
    {
        BOOT_IMAGE_LOGE("%s: decode_frame failed, ret=%d\n", __func__, ret);
        goto fail;
    }

    boot_image_jpeg_destroy(&dec);

    /* Pack the macroblock-aligned NV12 down to a tight out_w-stride frame. */
    boot_image_nv12_pack(out_buf, out_w, out_h, aln_w, aln_h);

    out->nv12       = out_buf;
    out->size       = tight_size;
    out->width      = out_w;
    out->height     = out_h;
    out->src_width  = (uint16_t)img_info.width;
    out->src_height = (uint16_t)img_info.height;

    BOOT_IMAGE_LOGI("%s: decoded %ux%u NV12 (aligned %ux%u, %u bytes)\n", __func__,
                    (unsigned)out_w, (unsigned)out_h,
                    (unsigned)aln_w, (unsigned)aln_h, (unsigned)tight_size);
    return AVDK_ERR_OK;

fail:
    boot_image_jpeg_destroy(&dec);
    if (out_buf != NULL)
    {
        bk_frame_buffer_free(out_buf);
    }
    return (ret != AVDK_ERR_OK) ? ret : AVDK_ERR_GENERIC;
}

avdk_err_t boot_image_decode(const uint8_t *stream, uint32_t len,
                             boot_image_format_t fmt,
                             boot_image_decoded_t *out)
{
    if (stream == NULL || len == 0U || out == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    os_memset(out, 0, sizeof(*out));

    switch (fmt)
    {
    case BOOT_IMAGE_FORMAT_JPEG:
        return boot_image_decode_jpeg(stream, len, out);
    default:
        BOOT_IMAGE_LOGW("%s: unsupported image format: %d\n", __func__, (int)fmt);
        return AVDK_ERR_UNSUPPORTED;
    }
}

void boot_image_decoded_free(boot_image_decoded_t *out)
{
    if (out == NULL)
    {
        return;
    }
    if (out->nv12 != NULL)
    {
        bk_frame_buffer_free(out->nv12);
        out->nv12 = NULL;
    }
    out->size = 0U;
}
