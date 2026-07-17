#include <common/bk_include.h>
#include <os/str.h>
#include <os/os.h>

#include "cJSON.h"

#include "doorbell_comm.h"
#include "doorbell_cmd.h"
#include "doorbell_devices.h"
#include "doorbell_rpc_internal.h"
#include "app_display.h"

#define TAG "db-rpc-lcd"
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* doorbell.lcd.turnOn : { lcdType: "mipi"|"rgb", lcdConfig: {...} }.
 * The device panel is fixed by board config; params are validated for shape
 * but the on-board panel configuration is used to light the screen. */
bk_err_t doorbell_rpc_lcd_turn_on(cJSON *params, cJSON *id)
{
    cJSON *lcd_type = params ? cJSON_GetObjectItem(params, "lcdType") : NULL;
    cJSON *lcd_cfg = params ? cJSON_GetObjectItem(params, "lcdConfig") : NULL;
    int ret;

    if (lcd_type == NULL || !cJSON_IsString(lcd_type) || lcd_cfg == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid lcd params", NULL);
    }

    if (os_strcmp(lcd_type->valuestring, "mipi") != 0 &&
        os_strcmp(lcd_type->valuestring, "rgb") != 0)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid lcdType", NULL);
    }

    if (cJSON_GetObjectItem(lcd_cfg, lcd_type->valuestring) == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "lcdConfig mismatch", NULL);
    }

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    bool lcd_vote_was_set = (doorbell_mm_service_get_status() & MM_STATUS_LCD_MASK) != 0;
    doorbell_mm_service_vote(MM_STATUS_LCD_BIT, true);
#endif

    ret = doorbell_display_turn_on(app_display_board_config_get());
    if (ret != BK_OK)
    {
        LOGE("doorbell_display_turn_on failed\n");
    }

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (ret != BK_OK && !lcd_vote_was_set)
    {
        doorbell_mm_service_vote(MM_STATUS_LCD_BIT, false);
    }
#endif

    if (ret != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "lcd turn on failed", NULL);
    }
    return doorbell_rpc_send_result_null(id);
}

/* doorbell.lcd.turnOff */
bk_err_t doorbell_rpc_lcd_turn_off(cJSON *params, cJSON *id)
{
    (void)params;
    int ret = doorbell_display_turn_off();

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (ret == BK_OK)
    {
        doorbell_mm_service_vote(MM_STATUS_LCD_BIT, false);
    }
#endif

    if (ret != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "lcd turn off failed", NULL);
    }
    return doorbell_rpc_send_result_null(id);
}

/* doorbell.lcd.getStatus : result.status = "on" | "off". */
bk_err_t doorbell_rpc_lcd_get_status(cJSON *params, cJSON *id)
{
    (void)params;
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    bool on = (doorbell_mm_service_get_status() & MM_STATUS_LCD_MASK) != 0;
    return doorbell_rpc_send_status(id, on ? "on" : "off");
#else
    return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "status unavailable", NULL);
#endif
}
