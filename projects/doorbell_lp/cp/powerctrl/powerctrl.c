#include <string.h>
#include <os/mem.h>
#include <os/os.h>
#include "os/str.h"
#include "powerctrl.h"
#include "db_ipc_msg.h"
#include "bk_ef.h"
#include <driver/pwr_clk.h>
#include "modules/cif_common.h"


#define PL_TAG "PL"
#define LOGI(...)   BK_LOGI(PL_TAG, ##__VA_ARGS__)
#define LOGW(...)   BK_LOGW(PL_TAG, ##__VA_ARGS__)
#define LOGE(...)   BK_LOGE(PL_TAG, ##__VA_ARGS__)
#define LOGD(...)   BK_LOGD(PL_TAG, ##__VA_ARGS__)
#define LOGV(...)   BK_LOGV(PL_TAG, ##__VA_ARGS__)
#define LOG_RAW(...)   BK_LOG_RAW(PL_TAG, ##__VA_ARGS__)

pl_wakeup_t pl_wakeup_env = {
    .wakeup_reason    = 0,
    .timer            = {0},
    .delay_action     = 0,
    .delay_arg1       = 0,
    .delay_arg2       = 0
};

void pl_wakeup_host(uint32_t flag)
{
    LOGI("%s %d\n", __func__, __LINE__);
    pl_set_wakeup_reason(flag);
    bk_pm_module_vote_boot_cp1_ctrl(PM_BOOT_CP1_MODULE_NAME_APP,PM_POWER_MODULE_STATE_ON);
    cif_power_up_host();
    bk_wifi_send_listen_interval_req(1);
}

void pl_power_down_host(void)
{
    LOGI("%s %d\n", __func__, __LINE__);
    pl_reset_wakeup_reason();
    bk_pm_module_vote_boot_cp1_ctrl(PM_BOOT_CP1_MODULE_NAME_APP,PM_POWER_MODULE_STATE_OFF);
    cif_power_down_host();
    bk_wifi_send_listen_interval_req(10);
}

void pl_start_lv_sleep(void)
{
    LOGI("%s %d\n", __func__, __LINE__);
    cif_start_lv_sleep();
}

void pl_exit_lv_sleep(void)
{
    LOGI("%s %d\n", __func__, __LINE__);
    cif_exit_sleep();
}