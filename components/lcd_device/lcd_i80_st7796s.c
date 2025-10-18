// Copyright 2020-2021 Beken
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

#include <driver/gpio.h>
#include <components/media_types.h>
#include <driver/lcd_types.h>
#include "lcd_disp_hal.h"

#define TAG "st7796s"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

//#if CONFIG_LCD_ST7796S

#define SW_RESET           0x01
#define SLEEP_OUT          0x11
#define COMMAND_1          0xf0
#define COMMAND_2          0xf0
#define PLOAR_CONVERT      0xb4
#define DISP_OUT_CTRL      0xe8
#define POWER_CTRL1        0xc1
#define POWER_CTRL2        0xc2
#define VCOM_CTRL          0xc5
#define CATHODE_CTRL       0xe0
#define ANODE_CTRL         0xe1
#define COLOR_MODE         0x3a
#define DISPLAY_ON         0x29
#define DISPLAY_OFF        0x28
#define ROW_SET            0x2b   //RASET
#define COLUMN_SET         0x2a   //CASET
#define RAM_WRITE          0x2c
#define CONTINUE_WRITE     0x3c
#define MEM_ACCESS_CTRL    0x36

#define TAG "st7796s"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)


const static uint32_t param_sleep_out[1]  = {0x00};
const static uint32_t param_command1[1]   = {0xc3};
const static uint32_t param_command2[1]   = {0x96};
const static uint32_t param_memacess[1]   = {0x48};
const static uint32_t param_color_md[1]   = {0x05};
const static uint32_t param_polar_cv[1]   = {0x01};
const static uint32_t param_disp_out[8]   = {0x40, 0x8a, 0x00, 0x00, 0x29, 0x19, 0xa5, 0x33};
const static uint32_t param_power_1[1]    = {0x06};
const static uint32_t param_power_2[1]    = {0xa7};
const static uint32_t param_vcom_ctl[1]   = {0x18};
const static uint32_t param_cath_ctl1[14] = {0xf0, 0x09, 0x0b, 0x06, 0x04, 0x15, 0x2f, 0x54, 0x42, 0x3c, 0x17, 0x14, 0x18, 0x1b};
const static uint32_t param_cath_ctl2[14] = {0xf0, 0x09, 0x0b, 0x06, 0x04, 0x03, 0x2d, 0x43, 0x42, 0x3b, 0x16, 0x14, 0x17, 0x1b};
const static uint32_t param_command12[1]  = {0x3c};
const static uint32_t param_command22[1]  = {0x69};
const static uint32_t param_display_on[1] = {0x00 };


static bk_err_t lcd_st7796s_init(const void *handle)
{
    if (!handle)
    {
        LOGE("%s i80_handle is NULL\n", __func__);
        return BK_FAIL;
    }

    bk_lcd_i80_handle_t *i80_handle = (bk_lcd_i80_handle_t *)handle;
    LOGI("%s\n", __func__);

    rtos_delay_milliseconds(131);
    rtos_delay_milliseconds(10);

    i80_handle->write_cmd(SLEEP_OUT, (uint32_t *)param_sleep_out , 0);
    rtos_delay_milliseconds(120);

    i80_handle->write_cmd(COMMAND_1, (uint32_t *)param_command1, 1);
    i80_handle->write_cmd(COMMAND_2, (uint32_t *)param_command2, 1);
    i80_handle->write_cmd(MEM_ACCESS_CTRL, (uint32_t *)param_memacess, 1);
    i80_handle->write_cmd(COLOR_MODE, (uint32_t *)param_color_md, 1);
    i80_handle->write_cmd(PLOAR_CONVERT, (uint32_t *)param_polar_cv, 1);
    i80_handle->write_cmd(DISP_OUT_CTRL, (uint32_t *)param_disp_out, 8);
    i80_handle->write_cmd(POWER_CTRL1, (uint32_t *)param_power_1, 1);
    i80_handle->write_cmd(POWER_CTRL2, (uint32_t *)param_power_2, 1);
    i80_handle->write_cmd(VCOM_CTRL, (uint32_t *)param_vcom_ctl, 1);
    i80_handle->write_cmd(CATHODE_CTRL, (uint32_t *)param_cath_ctl1, 14);
    i80_handle->write_cmd(ANODE_CTRL, (uint32_t *)param_cath_ctl2, 14);
    i80_handle->write_cmd(COMMAND_1, (uint32_t *)param_command12, 1);
    i80_handle->write_cmd(COMMAND_2, (uint32_t *)param_command22, 1);

    rtos_delay_milliseconds(120);
    i80_handle->write_cmd( DISPLAY_ON, (uint32_t *)param_display_on, 1);
    return BK_OK;
}

static bk_err_t st7796s_lcd_off(const void *handle)
{
    bk_lcd_i80_handle_t *i80_handle = (bk_lcd_i80_handle_t *)handle;
    i80_handle->write_cmd(DISPLAY_OFF, NULL, 0);
	return BK_OK;
}


static void lcd_st7796s_set_display_mem_area(const void *handle, uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye)
{
    bk_lcd_i80_handle_t *i80_handle = (bk_lcd_i80_handle_t *)handle;
    uint16 xs_l, xs_h, xe_l, xe_h;
    uint16 ys_l, ys_h, ye_l, ye_h;

    xs_h = (xs >> 8) & 0xff;
    xs_l = xs & 0xff;

    xe_h = ((xe - 1) >> 8) & 0xff;
    xe_l = (xe - 1) & 0xff;

    ys_h = (ys >> 8) & 0xff;
    ys_l = ys & 0xff;

    ye_h = ((ye - 1) >> 8) & 0xff;
    ye_l = (ye - 1) & 0xff;

    uint32_t param_clumn[4] = {xs_h, xs_l, xe_h, xe_l};
    uint32_t param_row[4] = {ys_h, ys_l, ye_h, ye_l};

    i80_handle->write_cmd(COLUMN_SET, param_clumn, 4);
    i80_handle->write_cmd(ROW_SET, param_row, 4);
}

static void lcd_st7796s_start_transfer(const void *handle)
{
    bk_lcd_i80_handle_t *i80_handle = (bk_lcd_i80_handle_t *)handle;
    i80_handle->write_cmd(RAM_WRITE, NULL, 0);
}

static const lcd_mcu_t lcd_mcu =
{
    .clk = LCD_40M,
    .set_mirror = NULL,
    .set_display_area = NULL,
    .start_transfer = lcd_st7796s_start_transfer,
    .continue_transfer = lcd_st7796s_start_transfer,
};

const lcd_device_t lcd_device_custom_st7796s =
{
	.name = "custom_lcd_st7796s",
	.type = LCD_TYPE_MCU8080,
	.width = 320,
	.height = 480,
	.mcu = &lcd_mcu,
	.init = lcd_st7796s_init,
	.off = st7796s_lcd_off,
};


