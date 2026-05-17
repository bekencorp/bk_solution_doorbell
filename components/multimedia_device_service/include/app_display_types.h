#pragma once
#include <components/bk_lcd_panel.h>      /* bk_display_dsi_panel_t */
#include <common/avdk_pixel_types.h>      /* bk_pixel_format_t */
#ifdef __cplusplus
extern "C" {
#endif



typedef struct
{
    struct {
        uint8_t enable;
        int8_t pin_reset;
        int8_t pin_backlight;
        int8_t pin_scl;
        int8_t pin_sda;
        const bk_display_dsi_panel_t *panel;
    } mipi;

    struct {
        bool enable;
        bool decompress;
        bk_pixel_format_t format;
    } dpu_video;

    struct {
        uint8_t enable;
    } rgb;
}   display_board_config_t;

typedef struct
{
    uint16_t id;
    uint16_t rotate;
    uint16_t fmt;
} display_parameters_ext_t;


#ifdef __cplusplus
}
#endif
