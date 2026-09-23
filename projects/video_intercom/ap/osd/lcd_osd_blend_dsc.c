#include "lcd_osd_assets.h"

const blend_info_t lcd_osd_assets[] = {
    {.name = "time", .addr = &font_clock, .content = "00:00:00"},
    {.name = "wifi_group", .addr = &wifi_rssi_none, .content = "wifi_rssi_none"},
    {.name = "wifi_group", .addr = &wifi_rssi_1, .content = "wifi_rssi_1"},
    {.name = "wifi_group", .addr = &wifi_rssi_2, .content = "wifi_rssi_2"},
    {.name = "wifi_group", .addr = &wifi_rssi_3, .content = "wifi_rssi_3"},
    {.name = "wifi_group", .addr = &wifi_rssi_full, .content = "wifi_rssi_full"},
    {.addr = NULL},
};

const blend_info_t lcd_osd_info[] = {
    {.name = "time", .addr = &font_clock, .content = "00:00:00"},
    {.name = "wifi_group", .addr = &wifi_rssi_none, .content = "wifi_rssi_none"},
    {.addr = NULL},
};
