#pragma once


#include <stdlib.h>
#include <string.h>
#include <common/bk_include.h>
#include "cli_config.h"
#include "bk_wifi_types.h"
#include "bk_wifi.h"
#include <os/str.h>
#include <os/mem.h>
#include <os/os.h>
#include <common/bk_err.h>
#include <components/log.h>
#include <components/event.h>
#include <common/sys_config.h>
#include <driver/uart.h>
#include "bk_uart.h"
#include <driver/uart.h>
#include <components/netif.h>
#include "bk_cli.h"
#include "cli.h"
#include "bk_private/bk_wifi.h"
#include "bk_wifi_private.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_KEY_STR_SIZE     64
#define MAX_VALUE_STR_SIZE   64
#define MAX_DB_IPC_MSG_SIZE    256

typedef enum{
    DB_IPC_CMD_BLE_DATA_TO_APK            = 0x0001,
    DB_IPC_CMD_KEEPALIVESTART             = 0x0002,
    DB_IPC_CMD_KEEPALIVESTOP              = 0x0003,
    DB_IPC_CMD_GET_WAKEUP_ENV_ADDR        = 0x0004,

    DB_IPC_EVENT_BLE_DATA_TO_USER         = 0x1001,
    DB_IPC_EVENT_KEEPALIVE_DISCONNECTION  = 0x1002,
}db_ipc_cmd_event;

typedef struct
{
    uint8_t      infotype;
    char         server[32];
    uint16_t     port;
    uint8_t      devId[32];
    uint8_t      idLen;
    uint8_t      key[64];
    uint8_t      keyLen;
}db_ipc_keepalive_cfg_t;

typedef struct
{
    uint32_t wakeup_reason;
    beken2_timer_t timer;
    uint8_t delay_action;
    uint32_t delay_arg1;
    uint32_t delay_arg2;
}pl_wakeup_t;

typedef struct
{
    uint16_t cmd_id;
    uint16_t len;
}db_ipc_msg_hdr_t;

extern pl_wakeup_t *pl_wakeup_env;

bk_err_t db_ipc_start_keepalive(const char *ip_addr, const char *cmd_port);
bk_err_t db_ipc_stop_keepalive(void);
int db_ipc_wakeup_env_init(void);
void db_ipc_wakeup_env_response(uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif