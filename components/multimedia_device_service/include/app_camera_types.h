#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <common/avdk_pixel_types.h>

typedef struct
{
    struct {
        uint8_t enable;
        uint8_t pin_scl;
        uint8_t pin_sda;
        uint8_t i2c_id;
        uint8_t pin_reset;
        uint8_t pin_pwdn;
        uint8_t pin_xclk;
        uint16_t sensor_max_width;
        uint16_t sensor_max_height;
        uint8_t sensor_fps;
    } mipi;

    struct {
        uint8_t enable;
        uint8_t pin_scl;
        uint8_t pin_sda;
        uint8_t i2c_id;
        uint8_t pin_reset;
        uint8_t pin_pwdn;
        uint8_t pin_xclk;
        uint16_t sensor_max_width;
        uint16_t sensor_max_height;
        uint8_t sensor_fps;
    } dvp;

    struct {
        uint8_t mp_enable;
        uint8_t mp_flexa;
        uint16_t mp_width;
        uint16_t mp_height;
        bk_pixel_format_t mp_format;
        uint8_t sp_enable;
        uint8_t sp_flexa;
        uint16_t sp_width;
        uint16_t sp_height;
        bk_pixel_format_t sp_format;
    } isp;
} camera_board_config_t;

typedef struct
{
    uint8_t fps;
    uint8_t port; //port id, useful for uvc, default is 1
    uint16_t id;
    uint16_t camera_width;
    uint16_t camera_height;
    uint16_t isp_output_width;
    uint16_t isp_output_height;
    uint16_t camera_out_format;
} camera_parameters_ext_t;

typedef struct
{
    uint8_t fps;
    uint8_t port; //port id, useful for uvc, default is 1
    uint16_t id;
    uint16_t width;
    uint16_t height;
    bk_image_format_t format;
} app_uvc_device_t;

#ifdef __cplusplus
}
#endif
