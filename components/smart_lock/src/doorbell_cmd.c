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
#define DOORBELL_NOTIFY_FLAGS (0xFF << 24)

typedef enum
{
    DBNOTIFY_HEARTBEAT = 1 | (DOORBELL_NOTIFY_FLAGS),
} dbnotify_t;

typedef struct
{
    beken_timer_t timer;
    uint32_t intval_ms;
    in_addr_t remote_address;
} db_cmd_info_t;


db_cmd_info_t *db_cmd_info = NULL;
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
static uint32_t mm_service_status = 0;
#endif

void doorbell_transmission_event_report(uint32_t opcode, uint8_t status, uint16_t flags)
{
    LOGD("%s, %d\n", __func__, opcode);

    db_evt_head_t evt;
    evt.opcode = CHECK_ENDIAN_UINT16(opcode);
    evt.status = status;
    evt.length = CHECK_ENDIAN_UINT16(0);
    evt.flags = flags;

    ntwk_trans_ctrl_send((uint8_t *)&evt, sizeof(db_evt_head_t));
}


static void doorbell_keep_alive_timer_handler(void *data)
{
    LOGD("doorbell_keep_alive_timer_handler\n");

    db_evt_head_t evt;
    evt.opcode = CHECK_ENDIAN_UINT16(DBNOTIFY_HEARTBEAT);
    evt.status = EVT_STATUS_OK;
    evt.length = CHECK_ENDIAN_UINT16(0);
    evt.flags = EVT_FLAGS_COMPLETE;

    ntwk_trans_ctrl_send((uint8_t *)&evt, sizeof(db_evt_head_t));
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

void doorbell_transmission_cmd_recive_callback(uint8_t *data, uint16_t length)
{
    db_cmd_head_t cmd, *ptr = (db_cmd_head_t *)data;
    uint8_t *p = ptr->payload;

    if (length < sizeof(db_cmd_head_t))
    {
        LOGE("cmd not enough\n");
        return;
    }

    cmd.opcode = CHECK_ENDIAN_UINT32(ptr->opcode);
    cmd.param = CHECK_ENDIAN_UINT32(ptr->param);
    cmd.length = CHECK_ENDIAN_UINT32(ptr->length);

    LOGD("%s, opcode: %u, param: %u, length: %u\n", __func__, cmd.opcode, cmd.param, cmd.length);
    #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    doorbell_keepalive_update_timestamp();
    #endif

    switch (cmd.opcode)
    {
        case DBCMD_SET_SERVICE_TYPE:
        {
            LOGD("DBCMD_SET_SOLUTION: %d, %s(%d)\n", cmd.opcode, ptr->payload, strlen((char *)ptr->payload));
            db_evt_head_t evt;
            doorbell_msg_t msg;

            evt.opcode = CHECK_ENDIAN_UINT16(cmd.opcode);
            evt.status = EVT_STATUS_OK;
            evt.length = CHECK_ENDIAN_UINT16(0);
            evt.flags = EVT_FLAGS_COMPLETE;

            if (!os_strncmp((const char *)ptr->payload, "doorbell-udp", strlen("doorbell-udp")))
            {
                msg.event = DBEVT_LAN_UDP_SERVICE_START_REQUEST;
                doorbell_send_msg(&msg);
            }
            else if (!os_strncmp((const char *)ptr->payload, "doorbell-tcp", strlen("doorbell-tcp")))
            {
                msg.event = DBEVT_LAN_TCP_SERVICE_START_REQUEST;
                doorbell_send_msg(&msg);
            }
            else
            {
                evt.status = EVT_STATUS_ERROR;

                LOGE("DBCMD_SET_SERVICE_TYPE error\n");
            }

            ntwk_trans_ctrl_send((uint8_t *)&evt, sizeof(db_evt_head_t));
        }
        break;

        case DBCMD_SET_KEEP_ALIVE:
        {
            LOGD("DBCMD_SET_KEEP_ALIVE: %u\n", cmd.param);

            if (cmd.param)
            {
                doorbell_cmd_server_init();
                doorbell_keep_alive_start_timer(cmd.param);
            }
            else
            {
                doorbell_keep_alive_stop_timer();
                doorbell_cmd_server_deinit();
            }

            doorbell_transmission_event_report(cmd.opcode, EVT_STATUS_OK, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_GET_SUPPORTED_CAMERA_DEVICES:
        {
            doorbell_get_supported_camera_devices(cmd.opcode);
        }
        break;

        case DBCMD_SET_CAMERA_TURN_ON:
        {
            if (cmd.length != sizeof(camera_parameters_t))
            {
                LOGV("error\n");
            }

            camera_parameters_t parameters = {0};

            parameters.id = cmd.param & 0xFFFF;
            STREAM_TO_UINT16(parameters.width, p);
            STREAM_TO_UINT16(parameters.height, p);
            STREAM_TO_UINT16(parameters.format, p);
            STREAM_TO_UINT16(parameters.protocol, p);
            if (cmd.length > 4 * sizeof(uint16_t))
            {
                STREAM_TO_UINT16(parameters.rotate, p);
            }
            else
            {
                parameters.rotate = -1;
            }

#ifdef CONFIG_STANDARD_DUALSTREAM
            if (cmd.length > 5 * sizeof(uint16_t))
            {
                STREAM_TO_UINT16(parameters.dualstream, p);
                STREAM_TO_UINT16(parameters.d_width, p);
                STREAM_TO_UINT16(parameters.d_height, p);
            }
#endif

#if (CONFIG_ASR_SERVICE_WITH_MIC)
            extern int doorbell_asr_turn_off(void);
            doorbell_asr_turn_off();
#endif

            int ret = doorbell_camera_turn_on(&parameters);
            if (ret != BK_OK)
            {
                LOGE("doorbell_camera_turn_on failed\n");
            }
            ret = doorbell_video_transfer_turn_on();
            if (ret != BK_OK)
            {
                LOGE("doorbell_video_transfer_turn_on failed\n");
            }

            #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ret == BK_OK)
            {
                doorbell_mm_service_vote(MM_STATUS_CAMERA_BIT, true);
            }
            #endif

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_SET_CAMERA_TURN_OFF:
        {
            doorbell_video_transfer_turn_off();
            int ret = doorbell_camera_turn_off();
            if (ret != BK_OK)
            {
                LOGE("doorbell_camera_turn_off failed\n");
            }

            #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ret == BK_OK)
            {
                doorbell_mm_service_vote(MM_STATUS_CAMERA_BIT, false);
            }
            #endif

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);

#if (CONFIG_ASR_SERVICE_WITH_MIC)
            extern int doorbell_asr_turn_on(void);
            doorbell_asr_turn_on();
#endif

        }
        break;

        case DBCMD_GET_CAMERA_STATUS:
        {

        }
        break;

        case DBCMD_SET_AUDIO_TURN_ON:
        {
#ifdef CONFIG_VOICE_SERVICE
            audio_parameters_t parameters;

            STREAM_TO_UINT8(parameters.aec, p);
            STREAM_TO_UINT8(parameters.uac, p);
            STREAM_TO_UINT32(parameters.rmt_recorder_sample_rate, p);
            STREAM_TO_UINT32(parameters.rmt_player_sample_rate, p);
            STREAM_TO_UINT8(parameters.rmt_recorder_fmt, p);
            STREAM_TO_UINT8(parameters.rmt_player_fmt, p);

#if (CONFIG_ASR_SERVICE_WITH_MIC)
            extern int doorbell_asr_turn_off(void);
            doorbell_asr_turn_off();
#endif
            int ret = doorbell_audio_turn_on(&parameters);
#else
            int ret = BK_FAIL;
#endif
            #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ret == BK_OK)
            {
                doorbell_mm_service_vote(MM_STATUS_AUDIO_BIT, true);
            }
            #endif

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_SET_AUDIO_TURN_OFF:
        {
            LOGD("DBCMD_SET_AUDIO_TURN_OFF\n");
#ifdef CONFIG_VOICE_SERVICE
            int ret = doorbell_audio_turn_off();
#else
            int ret = BK_FAIL;
#endif
            #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ret == BK_OK)
            {
                doorbell_mm_service_vote(MM_STATUS_AUDIO_BIT, false);
            }
            #endif

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);

#if (CONFIG_ASR_SERVICE_WITH_MIC)
            extern int doorbell_asr_turn_on(void);
            doorbell_asr_turn_on();
#endif
        }
        break;

        case DBCMD_GET_AUDIO_STATUS:
        {

        }
        break;

        case DBCMD_SET_LCD_TURN_ON:
        {
            //display_parameters_t parameters = {0};
            //STREAM_TO_UINT16(parameters.id, p);
            //STREAM_TO_UINT16(parameters.rotate_angle, p);
            //STREAM_TO_UINT16(parameters.pixel_format, p);

            int ret = doorbell_display_turn_on(app_display_board_config_get());
            if (ret != BK_OK)
            {
                LOGE("doorbell_display_turn_on failed\n");
            }
            #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ret == BK_OK)
            {
                doorbell_mm_service_vote(MM_STATUS_LCD_BIT, true);
            }
            #endif
            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_SET_LCD_TURN_OFF:
        {
            int ret = doorbell_display_turn_off();
            if (ret != BK_OK)
            {
                LOGE("doorbell_display_turn_off failed\n");
            }
            #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            if (ret == BK_OK)
            {
                doorbell_mm_service_vote(MM_STATUS_LCD_BIT, false);
            }
            #endif
            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_GET_SUPPORTED_LCD_DEVICES:
        {
            doorbell_get_supported_lcd_devices(cmd.opcode);
            #if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
            doorbell_keepalive_start_mm_status_check();
            #endif
        }
        break;

        case DBCMD_GET_LCD_STATUS:
        {
            doorbell_get_lcd_status(cmd.opcode);

        }
        break;

        case DBCMD_PNG:
        {
            doorbell_transmission_event_report(cmd.opcode, EVT_STATUS_OK, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_SET_ACOUSTICS:
        {
#ifdef CONFIG_VOICE_SERVICE
            uint32_t param;
            STREAM_TO_UINT32(param, p);

            int ret = doorbell_audio_acoustics(cmd.param, param);
#else
            int ret = BK_FAIL;
#endif
            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
        case DBCMD_WAKE_UP_REQUEST:
        {
            LOGD("DBCMD_WAKE_UP_REQUEST\n");
            doorbell_transmission_event_report(cmd.opcode, EVT_STATUS_OK, EVT_FLAGS_COMPLETE);
            doorbell_keepalive_start_mm_status_check();
        }
        break;
#endif
        default:
        {
            doorbell_transmission_event_report(cmd.opcode, EVT_STATUS_UNKNOWN, EVT_FLAGS_COMPLETE);
        }
        break;
    }
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
        default:
            LOGE("%s: invalid service bit %d\n", __func__, service_bit);
            return mm_service_status;
    }

    if (vote_add)
    {
        // Add vote (set bit)
        mm_service_status |= bit_mask;
        LOGD("%s: Service bit %d vote added, status: 0x%x\n", __func__, service_bit, mm_service_status);
    }
    else
    {
        // Remove vote (clear bit)
        mm_service_status &= ~bit_mask;
        LOGD("%s: Service bit %d vote removed, status: 0x%x\n", __func__, service_bit, mm_service_status);
    }

    LOGD("%s: status=0x%x\n", __func__, mm_service_status);
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