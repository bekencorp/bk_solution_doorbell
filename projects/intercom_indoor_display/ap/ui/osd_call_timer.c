// Copyright 2020-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// In-call duration OSD (WeChat-style MM:SS / H:MM:SS). LVGL is torn down
// during the video call, so digits are blitted into an ARGB8888 strip and
// shown via the compositor overlay.

#include "osd_call_timer.h"
#include "osd_digit_font.h"

#include <os/mem.h>
#include <string.h>
#include <stdio.h>

static uint32_t *s_timer_argb;

/* Digit color: match the incoming-call Answer button green (0x22C55E, see
 * incoming_call_init.c incoming_call_answer_btn bg_color). */
#define OSD_TIMER_R  (0x22)
#define OSD_TIMER_G  (0xC5)
#define OSD_TIMER_B  (0x5E)

const uint32_t *osd_call_timer_argb(void)
{
    return s_timer_argb;
}

bk_err_t osd_call_timer_acquire(void)
{
    if (s_timer_argb != NULL)
    {
        return BK_OK;
    }
    s_timer_argb = (uint32_t *)psram_zalloc(OSD_CALL_TIMER_W * OSD_CALL_TIMER_H * sizeof(uint32_t));
    return (s_timer_argb != NULL) ? BK_OK : BK_ERR_NO_MEM;
}

void osd_call_timer_release(void)
{
    if (s_timer_argb != NULL)
    {
        psram_free(s_timer_argb);
        s_timer_argb = NULL;
    }
}

static int glyph_index(char c)
{
    if (c >= '0' && c <= '9')
    {
        return (int)(c - '0');
    }
    if (c == ':')
    {
        return 10;
    }
    return -1;
}

static int string_width(const char *s)
{
    int w = 0;
    int last_adv = 0;
    int last_gw = 0;

    for (; *s != '\0'; s++)
    {
        int idx = glyph_index(*s);

        if (idx < 0)
        {
            continue;
        }
        last_adv = g_osd_digits[idx].advance;
        last_gw = g_osd_digits[idx].w;
        w += last_adv;
    }
    if (w > 0)
    {
        w = w - last_adv + last_gw;
    }
    return w;
}

static void blend_pixel(int x, int y, uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t *px;
    uint8_t da, dr, dg, db, out_a, inv;

    if (a == 0 || x < 0 || y < 0 || x >= OSD_CALL_TIMER_W || y >= OSD_CALL_TIMER_H)
    {
        return;
    }
    px = &s_timer_argb[y * OSD_CALL_TIMER_W + x];
    da = (uint8_t)((*px >> 24) & 0xFF);
    dr = (uint8_t)((*px >> 16) & 0xFF);
    dg = (uint8_t)((*px >> 8) & 0xFF);
    db = (uint8_t)(*px & 0xFF);

    inv = (uint8_t)(255 - a);
    out_a = (uint8_t)(a + (uint16_t)da * inv / 255);
    if (out_a == 0)
    {
        *px = 0;
        return;
    }
    dr = (uint8_t)(((uint16_t)r * a + (uint16_t)dr * da * inv / 255) / out_a);
    dg = (uint8_t)(((uint16_t)g * a + (uint16_t)dg * da * inv / 255) / out_a);
    db = (uint8_t)(((uint16_t)b * a + (uint16_t)db * da * inv / 255) / out_a);
    *px = ((uint32_t)out_a << 24) | ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | db;
}

static void blit_glyph(int x0, int y0, const osd_digit_glyph_t *gl,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a_scale)
{
    int x, y;

    for (y = 0; y < OSD_DIGIT_H; y++)
    {
        for (x = 0; x < gl->w; x++)
        {
            uint8_t a = (uint8_t)((uint16_t)gl->alpha[y * gl->w + x] * a_scale / 255);

            blend_pixel(x0 + x, y0 + y, a, r, g, b);
        }
    }
}

static void blit_text(int x0, int y0, const char *s)
{
    int x = x0;
    static const int k_halo[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };

    for (; *s != '\0'; s++)
    {
        int idx = glyph_index(*s);
        int i;

        if (idx < 0)
        {
            continue;
        }
        for (i = 0; i < 8; i++)
        {
            blit_glyph(x + k_halo[i][0], y0 + k_halo[i][1],
                       &g_osd_digits[idx], 0x18, 0x18, 0x18, 180);
        }
        blit_glyph(x, y0, &g_osd_digits[idx], OSD_TIMER_R, OSD_TIMER_G, OSD_TIMER_B, 255);
        x += g_osd_digits[idx].advance;
    }
}

void osd_call_timer_paint(uint32_t elapsed_sec)
{
    char buf[12];
    uint32_t h, m, s;
    int tw, x0, y0;

    s = elapsed_sec % 60U;
    m = (elapsed_sec / 60U) % 60U;
    h = elapsed_sec / 3600U;
    if (h > 99U)
    {
        h = 99U;
    }
    if (h > 0U)
    {
        snprintf(buf, sizeof(buf), "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)m, (unsigned)s);
    }

    if (s_timer_argb == NULL)
    {
        return;
    }
    memset(s_timer_argb, 0, OSD_CALL_TIMER_W * OSD_CALL_TIMER_H * sizeof(uint32_t));
    tw = string_width(buf);
    x0 = (OSD_CALL_TIMER_W - tw) / 2;
    y0 = (OSD_CALL_TIMER_H - OSD_DIGIT_H) / 2;
    if (x0 < 2)
    {
        x0 = 2;
    }
    blit_text(x0, y0, buf);
}
