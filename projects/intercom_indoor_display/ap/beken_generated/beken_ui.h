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
#define SCREEN_WIDTH    720
#define SCREEN_HEIGHT   1280

typedef struct
{
    /* Page: 0 objects */
    lv_obj_t *home;
    lv_obj_t *home_status_bar;
    lv_obj_t *home_btn_settings;
    lv_obj_t *home_btn_settings_label;
    lv_obj_t *home_st_settings;
    lv_obj_t *home_st_set_lbl;
    lv_obj_t *home_st_wifi;
    lv_obj_t *home_st_wifi_on;
    lv_obj_t *home_time_card;
    lv_obj_t *home_time_clock;
    lv_obj_t *home_home_week;
    lv_obj_t *home_home_date;
    lv_obj_t *home_notify_card;
    lv_obj_t *home_notify_hdr;
    lv_obj_t *home_notify_body;
    lv_obj_t *home_fc_call;
    lv_obj_t *home_img_call;
    lv_obj_t *home_lb_call;
    lv_obj_t *home_fc_records;
    lv_obj_t *home_img_records;
    lv_obj_t *home_lb_records;
    lv_obj_t *home_fc_property;
    lv_obj_t *home_img_property;
    lv_obj_t *home_lb_property;
    lv_obj_t *home_fc_monitor;
    lv_obj_t *home_img_monitor;
    lv_obj_t *home_lb_monitor;
    lv_obj_t *home_fc_scenes;
    lv_obj_t *home_img_scenes;
    lv_obj_t *home_lb_scenes;
    lv_obj_t *home_fc_access;
    lv_obj_t *home_img_access;
    lv_obj_t *home_lb_access;
    /* Page: 1 objects */
    lv_obj_t *provisioning;
    lv_obj_t *provisioning_status_bar;
    lv_obj_t *provisioning_btn_back;
    lv_obj_t *provisioning_btn_back_label;
    lv_obj_t *provisioning_ic_back;
    lv_obj_t *provisioning_btn_gohome;
    lv_obj_t *provisioning_btn_gohome_label;
    lv_obj_t *provisioning_ic_gohome;
    lv_obj_t *provisioning_qr_subtitle;
    lv_obj_t *provisioning_qr_instr;
    lv_obj_t *provisioning_qr_card;
    lv_obj_t *provisioning_qr_code;
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
    lv_obj_t *settings_status_bar;
    lv_obj_t *settings_btn_back;
    lv_obj_t *settings_btn_back_label;
    lv_obj_t *settings_ic_back;
    lv_obj_t *settings_btn_gohome;
    lv_obj_t *settings_btn_gohome_label;
    lv_obj_t *settings_ic_gohome;
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
    lv_obj_t *settings_fw_card;
    lv_obj_t *settings_fw_val;
    lv_obj_t *settings_fw_upd;
    lv_obj_t *settings_fw_upd_label;
    lv_obj_t *settings_btn_restart;
    lv_obj_t *settings_btn_restart_label;
    lv_obj_t *settings_btn_reset;
    lv_obj_t *settings_btn_reset_label;
    lv_obj_t *settings_sec_lang;
    lv_obj_t *settings_panel_lang;
    lv_obj_t *settings_lbl_lang;
    lv_obj_t *settings_dd_lang;
    lv_obj_t* settings_dd_lang_list;
    lv_obj_t *settings_btn_provision;
    lv_obj_t *settings_btn_provision_label;
    /* Page: 3 objects */
    lv_obj_t *incoming_call;
    lv_obj_t *incoming_call_avatar_frame;
    lv_obj_t *incoming_call_avatar_img;
    lv_obj_t *incoming_call_caller_name;
    lv_obj_t *incoming_call_call_status;
    lv_obj_t *incoming_call_decline_btn;
    lv_obj_t *incoming_call_decline_btn_label;
    lv_obj_t *incoming_call_dec_ic;
    lv_obj_t *incoming_call_answer_btn;
    lv_obj_t *incoming_call_answer_btn_label;
    lv_obj_t *incoming_call_ans_ic;
    lv_obj_t *incoming_call_dec_lbl;
    lv_obj_t *incoming_call_ans_lbl;
    lv_obj_t *incoming_call_lock_btn;
    lv_obj_t *incoming_call_lock_btn_label;
    lv_obj_t *incoming_call_lock_ic;
    lv_obj_t *incoming_call_lock_ic_open;
    lv_obj_t *incoming_call_lock_lbl;
} bk_lv_ui_t;

void init_page_home(bk_lv_ui_t *bk_ui);
void destroy_page_home(bk_lv_ui_t *bk_ui);
void init_page_provisioning(bk_lv_ui_t *bk_ui);
void destroy_page_provisioning(bk_lv_ui_t *bk_ui);
void init_page_settings(bk_lv_ui_t *bk_ui);
void destroy_page_settings(bk_lv_ui_t *bk_ui);
void init_page_incoming_call(bk_lv_ui_t *bk_ui);
void destroy_page_incoming_call(bk_lv_ui_t *bk_ui);

/* declare image */
LV_IMAGE_DECLARE(answer_call_copy_96x96_RGB565A8_NONE);
LV_IMAGE_DECLARE(beken_logo_240x240_RGB565A8_NONE);
LV_IMAGE_DECLARE(decline_call2_copy_96x96_RGB565A8_NONE);
LV_IMAGE_DECLARE(home_bg_particles_RGB565);
LV_IMAGE_DECLARE(ic_access_copy_144x144_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_back_return_v4_54x54_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_call_copy_144x144_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_home_36_54x54_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_lock_open_72_72x72_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_monitor_copy_144x144_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_property_copy_144x144_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_records_copy_144x144_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_scenes_copy_144x144_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_settings36_copy_54x54_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_wifi_off_copy_54x54_RGB565A8_NONE);
LV_IMAGE_DECLARE(ic_wifi_on_36_54x54_RGB565A8_NONE);
LV_IMAGE_DECLARE(lock_closed_72_copy_72x72_RGB565A8_NONE);

/* declare fonts */
LV_FONT_DECLARE(lv_font_montserrat_regular_16);
LV_FONT_DECLARE(lv_font_simhei_40);
LV_FONT_DECLARE(lv_font_simhei_72);
LV_FONT_DECLARE(lv_font_simhei_36);
LV_FONT_DECLARE(lv_font_simhei_34);
LV_FONT_DECLARE(lv_font_simhei_30);
LV_FONT_DECLARE(lv_font_simhei_28);
LV_FONT_DECLARE(lv_font_simhei_50);
LV_FONT_DECLARE(lv_font_montserrat_regular_20);

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
