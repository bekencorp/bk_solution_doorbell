/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 * 
 * This software is proprietary and confidential. No part of this software may be
 * reproduced, distributed, or transmitted in any form or by any means, including
 * photocopying, recording, or other electronic or mechanical methods, without the
 * prior written permission of BekenCorp, except in the case of brief quotations
 * embodied in critical reviews and certain other noncommercial uses permitted
 * by copyright law.
 * 
 * For permission requests, write to BekenCorp at armino_support@bekencorp.com.

 * Author: Beken LVGL Designer Tool
*/
/**
 * @file beken_ui.c
 * @brief Beken UI implementation file
 * 
 * This file contains the implementation of the Beken UI system.
 * Customers can modify this file to customize their UI without
 * touching the main application code or build system.
 */

#ifndef __BEKEN_UI_H__
#define __BEKEN_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* Display configuration */
#define SCREEN_WIDTH    1080
#define SCREEN_HEIGHT   1920

typedef struct
{
    /* Page: 0 objects */
    lv_obj_t *home;
    lv_obj_t *home_status_bar;
    lv_obj_t *home_clock;
    lv_obj_t *home_dev_title;
    lv_obj_t *home_net_label;
    lv_obj_t *home_video_card;
    lv_obj_t *home_feed_img;
    lv_obj_t *home_live_badge;
    lv_obj_t *home_rec_timer;
    lv_obj_t *home_feed_cap;
    lv_obj_t *home_cap_name;
    lv_obj_t *home_cap_sub;
    lv_obj_t *home_btn_settings;
    lv_obj_t *home_btn_settings_label;
    lv_obj_t *home_btn_adddev;
    lv_obj_t *home_btn_adddev_label;
    /* Page: 1 objects */
    lv_obj_t *provisioning;
    lv_obj_t *provisioning_back_btn;
    lv_obj_t *provisioning_back_btn_label;
    lv_obj_t *provisioning_qr_title;
    lv_obj_t *provisioning_qr_subtitle;
    lv_obj_t *provisioning_qr_instr;
    lv_obj_t *provisioning_qr_card;
    lv_obj_t *provisioning_qr_code;
    lv_obj_t *provisioning_btn_home;
    lv_obj_t *provisioning_btn_home_label;
    lv_obj_t *provisioning_info_card;
    lv_obj_t *provisioning_info_hdr;
    lv_obj_t *provisioning_k_name;
    lv_obj_t *provisioning_v_name;
    lv_obj_t *provisioning_k_mac;
    lv_obj_t *provisioning_v_mac;
    lv_obj_t *provisioning_k_model;
    lv_obj_t *provisioning_v_model;
    lv_obj_t *provisioning_k_proto;
    lv_obj_t *provisioning_v_proto;
    /* Page: 2 objects */
    lv_obj_t *settings;
    lv_obj_t *settings_back_btn;
    lv_obj_t *settings_back_btn_label;
    lv_obj_t *settings_set_title;
    lv_obj_t *settings_sec_a;
    lv_obj_t *settings_panel_a;
    lv_obj_t *settings_lbl_motion;
    lv_obj_t *settings_sw_motion;
    lv_obj_t *settings_lbl_chime;
    lv_obj_t *settings_sw_chime;
    lv_obj_t *settings_lbl_night;
    lv_obj_t *settings_sw_night;
    lv_obj_t *settings_lbl_vol;
    lv_obj_t *settings_sld_vol;
    lv_obj_t *settings_vol_val;
    lv_obj_t *settings_sec_b;
    lv_obj_t *settings_panel_b;
    lv_obj_t *settings_lbl_lock;
    lv_obj_t *settings_sw_lock;
    lv_obj_t *settings_lbl_tamper;
    lv_obj_t *settings_sw_tamper;
    lv_obj_t *settings_lbl_privacy;
    lv_obj_t *settings_sw_privacy;
    lv_obj_t *settings_fw_card;
    lv_obj_t *settings_fw_lbl;
    lv_obj_t *settings_fw_val;
    lv_obj_t *settings_fw_upd;
    lv_obj_t *settings_fw_upd_label;
    lv_obj_t *settings_btn_restart;
    lv_obj_t *settings_btn_restart_label;
    lv_obj_t *settings_btn_reset;
    lv_obj_t *settings_btn_reset_label;
    lv_obj_t *settings_btn_home_s;
    lv_obj_t *settings_btn_home_s_label;
} bk_lv_ui_t;

void init_page_home(bk_lv_ui_t *bk_ui);
void destroy_page_home(bk_lv_ui_t *bk_ui);
void init_page_provisioning(bk_lv_ui_t *bk_ui);
void destroy_page_provisioning(bk_lv_ui_t *bk_ui);
void init_page_settings(bk_lv_ui_t *bk_ui);
void destroy_page_settings(bk_lv_ui_t *bk_ui);

/* declare image */
LV_IMAGE_DECLARE(door_cam_1024x1536_RGB565A8_NONE);

/* declare fonts */
LV_FONT_DECLARE(lv_font_montserrat_regular_36);
LV_FONT_DECLARE(lv_font_montserrat_regular_30);
LV_FONT_DECLARE(lv_font_montserrat_regular_26);
LV_FONT_DECLARE(lv_font_montserrat_regular_28);
LV_FONT_DECLARE(lv_font_montserrat_regular_32);
LV_FONT_DECLARE(lv_font_montserrat_regular_24);
LV_FONT_DECLARE(lv_font_montserrat_regular_34);
LV_FONT_DECLARE(lv_font_montserrat_regular_44);

/**
 * @brief Initialize the Beken UI system
 * 
 * This function initializes the UI components and creates the main interface.
 * Customers can modify this function to customize their UI layout.
 */
void beken_ui_init(void);

/**
 * @brief Get the configured screen width
 * @return Screen width in pixels
 */
int beken_get_screen_width(void);

/**
 * @brief Get the configured screen height
 * @return Screen height in pixels
 */
int beken_get_screen_height(void);

extern bk_lv_ui_t bk_lv_tool_ui;

/* Digital clock functions */
void lv_digital_clock_timer(lv_timer_t *timer);
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second);
void lv_digital_clock_unregister(lv_obj_t *label);
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second);
void lv_digital_clock_unregister(lv_obj_t *label);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* __BEKEN_UI_H__ */
