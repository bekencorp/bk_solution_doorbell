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
#include "lvgl.h"
#include "beken_ui.h"
#include <stdio.h>


/* Digital clock global timer and instance management */
#define MAX_DIGITAL_CLOCKS 32

typedef struct {
    lv_obj_t *label;      /* label object pointer */
    int show_second;      /* show second */
    int use_ampm;         /* use ampm */
} lv_digital_clock_inst_t;

static lv_timer_t *g_digital_clock_timer = NULL;
static lv_digital_clock_inst_t g_digital_clock_instances[MAX_DIGITAL_CLOCKS];
static int g_digital_clock_count = 0;
static int g_global_hour = 0;
static int g_global_minute = 0;
static int g_global_second = 0;
static char g_global_meridiem[4] = "AM";
static int g_timer_initialized = 0;

/**
 * @brief format time string
 * @param buf output buffer
 * @param hour hour
 * @param minute minute
 * @param second second (if show_second is 0, it will be ignored)
 * @param use_ampm use ampm
 * @param show_second show second
 */
static void format_time_string(char *buf, int hour, int minute, int second, int use_ampm, int show_second)
{
    int display_hour = hour;
    const char *meridiem = "";
    
    if (use_ampm) {
        if (hour == 0) {
            display_hour = 12;
            meridiem = "AM";
        } else if (hour < 12) {
            display_hour = hour;
            meridiem = "AM";
        } else if (hour == 12) {
            display_hour = 12;
            meridiem = "PM";
        } else {
            display_hour = hour - 12;
            meridiem = "PM";
        }
        
        if (show_second) {
            lv_snprintf(buf, 32, "%02d:%02d:%02d %s", display_hour, minute, second, meridiem);
        } else {
            lv_snprintf(buf, 32, "%02d:%02d %s", display_hour, minute, meridiem);
        }
    } else {
        if (show_second) {
            lv_snprintf(buf, 32, "%02d:%02d:%02d", hour, minute, second);
        } else {
            lv_snprintf(buf, 32, "%02d:%02d", hour, minute);
        }
    }
}

/**
* @brief global digital clock timer callback
 * @param timer LVGL timer object
 */
void lv_digital_clock_timer(lv_timer_t *timer)
{
    int i;
    char time_str[32];
    
    g_global_second++;
    if (g_global_second >= 60) {
        g_global_second = 0;
        g_global_minute++;
    }
    if (g_global_minute >= 60) {
        g_global_minute = 0;
        g_global_hour++;
    }
    if (g_global_hour >= 24) {
        g_global_hour = 0;
    }
    
    if (g_global_hour == 0) {
        lv_strcpy(g_global_meridiem, "AM");
    } else if (g_global_hour == 12) {
        lv_strcpy(g_global_meridiem, "PM");
    }
    
    i = 0;
    while (i < g_digital_clock_count) {
        if (g_digital_clock_instances[i].label != NULL && 
            lv_obj_is_valid(g_digital_clock_instances[i].label) &&
            lv_obj_check_type(g_digital_clock_instances[i].label, &lv_label_class)) {
            format_time_string(time_str, g_global_hour, g_global_minute, g_global_second,
                             g_digital_clock_instances[i].use_ampm,
                             g_digital_clock_instances[i].show_second);
            lv_label_set_text(g_digital_clock_instances[i].label, time_str);
            i++;
        } else {
            int j;
            for (j = i; j < g_digital_clock_count - 1; j++) {
                g_digital_clock_instances[j] = g_digital_clock_instances[j + 1];
            }
            g_digital_clock_count--;
        }
    }
}

/**
 * @brief register digital_clock instance (called when create)
 * @param label label object pointer
 * @param show_second show second
 * @param use_ampm use ampm
 * @param hour initial hour (optional, -1 means not specified, only the first instance is valid)
 * @param minute initial minute (optional, -1 means not specified, only the first instance is valid)
 * @param second initial second (optional, -1 means not specified, only the first instance is valid)
 */
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second)
{
    if (g_digital_clock_count >= MAX_DIGITAL_CLOCKS) {
        return;
    }
    
    if (g_timer_initialized == 0) {
        if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60) {
            g_global_hour = hour;
            g_global_minute = minute;
            g_global_second = second;
        } else {
            g_global_hour = 0;
            g_global_minute = 0;
            g_global_second = 0;
        }
        
        if (use_ampm) {
            if (g_global_hour == 0) {
                lv_strcpy(g_global_meridiem, "AM");
            } else if (g_global_hour < 12) {
                lv_strcpy(g_global_meridiem, "AM");
            } else {
                lv_strcpy(g_global_meridiem, "PM");
            }
        }
        
        if (g_digital_clock_timer == NULL) {
            g_digital_clock_timer = lv_timer_create(lv_digital_clock_timer, 1000, NULL);
        }
        g_timer_initialized = 1;
    }

    g_digital_clock_instances[g_digital_clock_count].label = label;
    g_digital_clock_instances[g_digital_clock_count].show_second = show_second;
    g_digital_clock_instances[g_digital_clock_count].use_ampm = use_ampm;
    g_digital_clock_count++;
}

/**
 * @brief unregister digital_clock instance (called when destroy)
 * @param label label object pointer
 */
void lv_digital_clock_unregister(lv_obj_t *label)
{
    int i, j;
    for (i = 0; i < g_digital_clock_count; i++) {
        if (g_digital_clock_instances[i].label == label) {
            for (j = i; j < g_digital_clock_count - 1; j++) {
                g_digital_clock_instances[j] = g_digital_clock_instances[j + 1];
            }
            g_digital_clock_count--;
            break;
        }
    }
}
