#include "db_ipc_msg.h"
#include <string.h>
#include <common/sys_config.h>
#include "bk_uart.h"
#include "bk_private/bk_wifi.h"
#include "bk_wifi_private.h"
#include "bk_cli.h"
#include "cli.h"
#include <components/event.h>
#include <components/netif.h>
#include "bk_wifi.h"
#include "bk_wifi_types.h"
#include <modules/wdrv_common.h>

#define DB_IPC_TAG "DB_IPC"

#define LOGI(...)       BK_LOGI(DB_IPC_TAG, ##__VA_ARGS__)
#define LOGW(...)       BK_LOGW(DB_IPC_TAG, ##__VA_ARGS__)
#define LOGE(...)       BK_LOGE(DB_IPC_TAG, ##__VA_ARGS__)
#define LOGD(...)       BK_LOGD(DB_IPC_TAG, ##__VA_ARGS__)
#define LOGV(...)       BK_LOGV(DB_IPC_TAG, ##__VA_ARGS__)


pl_wakeup_t *pl_wakeup_env = NULL;

bk_err_t db_ipc_start_keepalive(const char *ip_addr, const char *cmd_port)
{
    int ret = BK_OK;
    db_ipc_keepalive_cfg_t cfg = {0};

    if (ip_addr == NULL || cmd_port == NULL) {
        LOGE("%s: Invalid parameters (ip_addr or cmd_port is NULL)\n", __func__);
        return BK_FAIL;
    }

    // Copy IP address and convert port string to integer
    os_strncpy(cfg.server, ip_addr, sizeof(cfg.server) - 1);
    cfg.server[sizeof(cfg.server) - 1] = '\0';
    cfg.port = (uint16_t)atoi(cmd_port);

    LOGI("%s: Sending keepalive command - IP: %s, Port: %s (%d)\n", 
         __func__, ip_addr, cmd_port, cfg.port);

    ret = bk_wdrv_customer_transfer(DB_IPC_CMD_KEEPALIVESTART, (uint8_t *)&cfg, sizeof(cfg));

    if (ret != 0) {
        LOGE("%s: Failed to send keepalive command: %d\n", __func__, ret);
    }

    return ret;
}

bk_err_t db_ipc_stop_keepalive(void)
{
    int ret = BK_OK;
    db_ipc_keepalive_cfg_t cfg = {0};

    os_memset(&cfg, 0, sizeof(db_ipc_keepalive_cfg_t));

    ret = bk_wdrv_customer_transfer(DB_IPC_CMD_KEEPALIVESTOP, (uint8_t *)&cfg, sizeof(cfg));
    if (ret != 0) {
        LOGE("%s: Failed to send DB_IPC_CMD_KEEPALIVESTOP: %d\n", __func__, ret);
        return BK_FAIL;
    } 

    return ret;
}

int db_ipc_wakeup_env_init(void)
{
    int ret;
    uint8_t response_buf[64] = {0};
    uint16_t response_len = 0;

    ret = bk_wdrv_customer_transfer_rsp(DB_IPC_CMD_GET_WAKEUP_ENV_ADDR, NULL, 0, 
                                                   response_buf, sizeof(response_buf), &response_len);
    if (ret != 0) {
        LOGE("%s %d Failed ret %d\n",__func__,__LINE__, ret);
        return -1;
    }

    if (response_len >= sizeof(uint32_t)) {
        db_ipc_wakeup_env_response(response_buf, response_len);
        if (pl_wakeup_env != NULL) {
            LOGV("%s %d Wakeup env address: 0x%p\n",__func__,__LINE__, pl_wakeup_env);
            return 0;
        } else {
            LOGE("%s %d Failed\n",__func__,__LINE__);
            return -1;
        }
    } else {
        LOGE("Invalid response length: %d \n", response_len);
        return -1;
    }
}

void db_ipc_wakeup_env_response(uint8_t *data, uint16_t len)
{
    if (data == NULL || len < sizeof(uint32_t)) {
        LOGE("Invalid wakeup env response, len: %d \n", len);
        pl_wakeup_env = NULL;
        return;
    }

    uint32_t addr = *(uint32_t *)data;
    pl_wakeup_env = (pl_wakeup_t *)(uintptr_t)addr;

    if (pl_wakeup_env != NULL) {
        LOGV("wakeup_reason: 0x%x\n", pl_wakeup_env->wakeup_reason);
    }
}







