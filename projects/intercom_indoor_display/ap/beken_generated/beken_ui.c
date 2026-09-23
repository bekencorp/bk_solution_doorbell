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
/*
 * @file: beken_ui.c
 * @brief: beken ui implementation file
 * This file contains the implementation of the Beken UI system.
 * Customers can modify the UI implementation in beken_ui.c without
 * touching the main application code.
 */

#include "beken_ui.h"
#include <stdio.h>

bk_lv_ui_t bk_lv_tool_ui = {0};

/**
 * @brief Get the configured screen width
 * @return Screen width in pixels
 */
int beken_get_screen_width(void)
{
    return SCREEN_WIDTH;
}

/**
 * @brief Get the configured screen height
 * @return Screen height in pixels
 */
int beken_get_screen_height(void)
{
    return SCREEN_HEIGHT;
}

/**
 * @brief Initialize the Beken UI system
 */
void beken_ui_init(void)
{
    
    init_page_home(&bk_lv_tool_ui);
    lv_screen_load(bk_lv_tool_ui.home);
}