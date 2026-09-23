#ifndef __LCD_OSD_ASSETS_H__
#define __LCD_OSD_ASSETS_H__

#include <stdint.h>
#include "components/bk_draw_osd.h"
#include "modules/lcd_font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Portrait panel after GPU rotate-90 (ER68576B 720x1280). OSD uses
 * osd_rotate_degree=0 in this space so time/WiFi sit on the top edge,
 * clustered left to stay clear of PIP (right/top of the composed frame).
 *
 * Time uses draw_osd_example font_clock (Roboto53, ~172px for HH:MM:SS),
 * so WiFi is placed to the right of that string. */
#define LCD_OSD_PANEL_W        720U
#define LCD_OSD_PANEL_H        1280U
#define LCD_OSD_TIME_X         24U
#define LCD_OSD_TIME_Y         16U
#define LCD_OSD_WIFI_X         240U
#define LCD_OSD_WIFI_Y         16U

#define LCD_OSD_WIFI_LEVELS    5U
#define LCD_OSD_WIFI_W         60U
#define LCD_OSD_WIFI_H         60U

extern const gui_font_digit_struct *const font_digit_Roboto53;
extern const bk_blend_t font_clock;

/* draw_osd_example wifi_group (60x60 ARGB8888), xpos/ypos remapped above. */
extern const bk_blend_t wifi_rssi_none;
extern const bk_blend_t wifi_rssi_1;
extern const bk_blend_t wifi_rssi_2;
extern const bk_blend_t wifi_rssi_3;
extern const bk_blend_t wifi_rssi_full;

/* Asset lookup table + initial display list (like draw_osd_example blend_dsc.c). */
extern const blend_info_t lcd_osd_assets[];
extern const blend_info_t lcd_osd_info[];

#ifdef __cplusplus
}
#endif

#endif /* __LCD_OSD_ASSETS_H__ */
