#include <common/bk_include.h>
#include "cli.h"
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/int.h>
#include <common/bk_err.h>
#include <getopt.h>

#include "lwip/sockets.h"
#include "lwip/udp.h"
#include "net.h"
#include "string.h"
#include <components/netif.h>

#include <common/bk_generic.h>

#include "doorbell_comm.h"
#include "doorbell_network.h"

#include "doorbell_devices.h"
#include "doorbell_audio_device.h"
#include "doorbell_cmd.h"
#include "doorbell_jsonrpc.h"
#include "app_display.h"

#include "network_transfer.h"

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
#include "doorbell_keepalive.h"
#endif

#define TAG "db-cmd"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define DOORBELL_CMD_BUFFER (1460)

typedef struct
{
    beken_timer_t timer;
    uint32_t intval_ms;
    in_addr_t remote_address;
} db_cmd_info_t;


db_cmd_info_t *db_cmd_info = NULL;
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
static uint32_t mm_service_status = 0;

void doorbell_transmission_device_power_on_notify(void)
{
    LOGI("Notify device power on to server\r\n");
    /* Control channel is JSON-framed: emit a JSON-RPC notification. */
    doorbell_jsonrpc_send_notify("doorbell.notify.powerOn", NULL);
}
#endif

static void doorbell_keep_alive_timer_handler(void *data)
{
    (void)data;
    LOGD("doorbell_keep_alive_timer_handler\n");
    doorbell_jsonrpc_send_notify("doorbell.notify.heartbeat", NULL);
}

int doorbell_keep_alive_start_timer(UINT32 time_ms)
{
    if (db_cmd_info)
    {
        int err;
        UINT32 org_ms = db_cmd_info->intval_ms;

        if (org_ms != 0)
        {
            if ((org_ms != time_ms))
            {
                if (db_cmd_info->timer.handle != NULL)
                {
                    err = rtos_deinit_timer(&db_cmd_info->timer);
                    if (BK_OK != err)
                    {
                        LOGE("deinit time fail\r\n");
                        return BK_FAIL;
                    }
                    db_cmd_info->timer.handle = NULL;
                }
            }
            else
            {
                LOGE("timer aready start\r\n");
                return BK_OK;
            }
        }

        err = rtos_init_timer(&db_cmd_info->timer,
                              time_ms,
                              doorbell_keep_alive_timer_handler,
                              NULL);
        if (BK_OK != err)
        {
            LOGE("init timer fail\r\n");
            return BK_FAIL;
        }
        db_cmd_info->intval_ms = time_ms;

        err = rtos_start_timer(&db_cmd_info->timer);
        if (BK_OK != err)
        {
            LOGE("start timer fail\r\n");
            return BK_FAIL;
        }
        LOGD("doorbell_keep_alive_start_timer\r\n");

        return BK_OK;
    }
    return BK_FAIL;
}

int doorbell_keep_alive_stop_timer(void)
{
    if (db_cmd_info)
    {
        int err;

        err = rtos_stop_timer(&db_cmd_info->timer);
        if (BK_OK != err)
        {
            LOGE("stop time fail\r\n");
            return BK_FAIL;
        }

        return BK_OK;
    }
    return BK_FAIL;
}

bk_err_t doorbell_cmd_server_init(void)
{
    if (db_cmd_info != NULL)
    {
        LOGE("db_cmd_info already init\n");
        return BK_FAIL;
    }

    db_cmd_info = os_malloc(sizeof(db_cmd_info_t));

    if (db_cmd_info == NULL)
    {
        LOGE("malloc db_cmd_info\n");
        return BK_FAIL;
    }

    os_memset(db_cmd_info, 0, sizeof(db_cmd_info_t));
    return BK_OK;
}

bk_err_t doorbell_cmd_server_deinit(void)
{

    if (db_cmd_info == NULL)
    {
        LOGE("db_cmd_info not init\n");
        return BK_FAIL;
    }

    os_free(db_cmd_info);
    db_cmd_info = NULL;
    return BK_OK;
}

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
uint32_t doorbell_mm_service_vote(mm_status_bit_t service_bit, bool vote_add)
{
    uint32_t bit_mask = 0;

    switch (service_bit)
    {
        case MM_STATUS_CAMERA_BIT:
            bit_mask = MM_STATUS_CAMERA_MASK;
            break;
        case MM_STATUS_AUDIO_BIT:
            bit_mask = MM_STATUS_AUDIO_MASK;
            break;
        case MM_STATUS_LCD_BIT:
            bit_mask = MM_STATUS_LCD_MASK;
            break;
        case MM_STATUS_ALL_BIT:
            bit_mask = MM_STATUS_ALL_MASK;
            break;
        default:
            LOGE("%s: invalid service bit %d\n", __func__, service_bit);
            return mm_service_status;
    }

    if (vote_add)
    {
        // Add vote (set bit)
        mm_service_status |= bit_mask;
        LOGD("%s: Service bit %d vote added, status: 0x%x\n", __func__, service_bit, mm_service_status);
        doorbell_keepalive_stop_mm_status_check();
    }
    else
    {
        // Remove vote (clear bit)
        mm_service_status &= ~bit_mask;
        LOGD("%s: Service bit %d vote removed, status: 0x%x\n", __func__, service_bit, mm_service_status);
        if (mm_service_status == 0)
        {
            LOGI("%s: All multimedia services are idle, start keepalive countdown\n", __func__);
            doorbell_keepalive_start_mm_status_check();
        }
    }

    LOGD("%s: status=0x%x\n", __func__, mm_service_status);

#if (CONFIG_ASR_SERVICE_WITH_MIC)
    /* Central choke point: a newly active service (incl. LCD) turns ASR off. */
    doorbell_asr_arbitrate();
#endif

    return mm_service_status;
}

uint32_t doorbell_mm_service_get_status(void)
{
    LOGD("%s: Status bitmap: 0x%x (Camera:%d, Audio:%d, LCD:%d)\n", 
         __func__, mm_service_status,
         (mm_service_status & MM_STATUS_CAMERA_MASK) ? 1 : 0,
         (mm_service_status & MM_STATUS_AUDIO_MASK) ? 1 : 0,
         (mm_service_status & MM_STATUS_LCD_MASK) ? 1 : 0);

    return mm_service_status;
}
#endif