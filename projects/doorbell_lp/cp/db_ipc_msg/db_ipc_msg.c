#include <os/str.h>
#include <os/mem.h>
#include <os/os.h>
#include "db_ipc_msg.h"
#include "bk_ef.h"
#include <components/log.h>
#include "modules/cif_common.h"
#include "doorbell_comm.h"
#include "powerctrl.h"
#include "db_keepalive.h"

#define DB_IPC_TAG "db_ipc"

#define LOGI(...) BK_LOGW(DB_IPC_TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(DB_IPC_TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(DB_IPC_TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(DB_IPC_TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(DB_IPC_TAG, ##__VA_ARGS__)


int db_ipc_send_event(uint16_t event_id, uint8_t *data, uint16_t len)
{
    int ret = 0;
    db_ipc_evt_t event = {0};

    if (len > MAX_DB_IPC_MSG_SIZE)
    {
        LOGE("%s %d error: len %d\n", __func__, __LINE__, len);
        return -1;
    }

    LOGV("%s %d event_id:%d len:%d\n", __func__, __LINE__, event_id, len);

    event.header.cid = event_id;
    event.header.len = len;
    event.header.magic = DB_IPC_PATTERN;

    if (data && len)
        os_memcpy(event.data, data, len);

    ret = cif_send_customer_event((uint8_t *)&event, sizeof(db_ipc_prote_hdr_t) + len);

    return ret;
}

int db_ipc_msg_handler(struct bk_msg_hdr *msg)
{
    int ret = 0;
    uint8_t *cfm_data = NULL;
    uint16_t cfm_data_len = 0;
    db_ipc_msg_hdr_t *db_ipc_cmd_hdr = NULL;

    if (msg == NULL)
    {
        LOGI("%s data invalid\n",__func__);
        return -1;
    }

    db_ipc_cmd_hdr = (db_ipc_msg_hdr_t *)(msg + 1);
    LOGI("%s cmd id:0x%x\n",__func__, db_ipc_cmd_hdr->cmd_id);
    switch (db_ipc_cmd_hdr->cmd_id)
    {
        #if !CONFIG_BTDM_CONTROLLER_ONLY
        case DB_IPC_CMD_BLE_DATA_TO_APK:
        {
            LOGI("add ble data \n");

            doorbell_msg_t msg = {0};
            uint8_t *cmd_data = NULL;

            cmd_data = (uint8_t*)os_malloc(db_ipc_cmd_hdr->len);
            if (cmd_data)
            {
                os_memcpy(cmd_data, db_ipc_cmd_hdr->payload, db_ipc_cmd_hdr->len);
                msg.event = DBEVT_DATA_TO_APK;
                msg.data = cmd_data;
                msg.len = db_ipc_cmd_hdr->len;
                doorbell_send_msg(&msg);
            }
            else
            {
                LOGE(" %s malloc failed \n",__func__);
            }
            break;
        }
        #endif
        case DB_IPC_CMD_KEEPALIVESTART:
        {
            db_ipc_keepalive_cfg_t *cfg = (db_ipc_keepalive_cfg_t *)(db_ipc_cmd_hdr + 1);
            LOGI("DB_IPC_CMD_KEEPALIVESTART, server=%s, port=%d\n", cfg->server, cfg->port);
            
            // Initialize and start keepalive with doorbell message format
            if (db_keepalive_cp_init(cfg) == BK_OK) {
                db_keepalive_cp_start();
            } else {
                LOGE("Failed to init keepalive\n");
            }
            break;
        }
        case DB_IPC_CMD_KEEPALIVESTOP:
        {
            db_keepalive_cp_stop();
            LOGI("DB_IPC_CMD_KEEPALIVESTOP\n");
            break;
        }
        case DB_IPC_CMD_GET_WAKEUP_ENV_ADDR:
        {
            static uint32_t addr;
            addr = (uint32_t)(uintptr_t)&pl_wakeup_env;
            cfm_data = (uint8_t *)&addr;
            cfm_data_len = sizeof(addr);
            LOGI("DB_IPC_CMD_GET_WAKEUP_ENV_ADDR: 0x%x\n", addr);
            break;
        }
        default:
            break;
    }

    if (cfm_data && cfm_data_len)
    {
        ret = cif_send_customer_cmd_cfm(cfm_data, cfm_data_len, msg);
    }
    else
    {
        ret = cif_send_customer_cmd_cfm(NULL, 0, msg);
    }

    return ret;
}


void db_ipc_msg_init(void)
{
    LOGI("%s\n", __func__);
    cif_register_customer_msg_handler(db_ipc_msg_handler);
}

void db_ipc_msg_deinit(void)
{
    LOGI("%s\n", __func__);
}

