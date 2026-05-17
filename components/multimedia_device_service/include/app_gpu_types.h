#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    struct {
        uint8_t enable;
        uint8_t degree;
        uint16_t src_width;
        uint16_t src_height;
        uint16_t dst_width;
        uint16_t dst_height;
        uint16_t tess_width;
        uint16_t tess_height;
        bk_pixel_format_t src_format;
        bk_pixel_format_t dst_format;
        bool scale;
        bool dst_compress;
    } flexa;
} gpu_board_config_t;




#ifdef __cplusplus
}
#endif
