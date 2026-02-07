#pragma once

#include <stdlib.h>
#include <string.h>
#include <common/bk_include.h>

#ifdef __cplusplus
extern "C" {
#endif


#define DB_IPC_DEBUG_CODE_MAGIC                  (0xAABBCCDD)

#define DB_IPC_PATTERN  (0xa5a6)
#define MAX_DB_IPC_MSG_SIZE   256
#define MAX_KEY_STR_SIZE     64
#define MAX_VALUE_STR_SIZE   64

#define DB_BK7258_VERSION "999.999.999.999"

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
    uint16_t magic;
    uint16_t cid;
    uint16_t ctl;
    uint16_t seq;
    uint16_t checksum;
    uint16_t len;
}db_ipc_prote_hdr_t;

typedef struct
{
    db_ipc_prote_hdr_t header;
    uint8_t data[MAX_DB_IPC_MSG_SIZE];
}db_ipc_evt_t;

typedef struct
{
    uint16_t cmd_id;
    uint16_t len;
    uint8_t payload[0];
}db_ipc_msg_hdr_t;


extern int db_ipc_send_event(uint16_t event_id, uint8_t *data, uint16_t len);
extern void db_ipc_msg_init(void);

#ifdef __cplusplus
}
#endif
