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

#include "network_transfer.h"

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
    DBCMD_SET_SERVICE_TYPE = 1,
    DBCMD_SET_KEEP_ALIVE = 2,
    DBCMD_GET_SUPPORTED_CAMERA_DEVICES = 3,
    DBCMD_GET_SUPPORTED_LCD_DEVICES = 4,
    DBCMD_GET_SUPPORTED_MIC_DEVICES = 5,
    DBCMD_GET_SUPPORTED_SPEAKER_DEVICES = 6,

    DBCMD_SET_CAMERA_TURN_ON = 7,
    DBCMD_SET_CAMERA_TURN_OFF = 8,
    DBCMD_GET_CAMERA_STATUS = 9,

    DBCMD_SET_AUDIO_TURN_ON = 10,
    DBCMD_SET_AUDIO_TURN_OFF = 11,
    DBCMD_GET_AUDIO_STATUS = 12,

    DBCMD_SET_LCD_TURN_ON = 13,
    DBCMD_SET_LCD_TURN_OFF = 14,
    DBCMD_GET_LCD_STATUS = 15,

    DBCMD_SET_ACOUSTICS = 16,

    DBCMD_PNG = 100,

} dbcmd_t;


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

            int ret = doorbell_camera_turn_on(&parameters);
            doorbell_video_transfer_turn_on();

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_SET_CAMERA_TURN_OFF:
        {
            doorbell_video_transfer_turn_off();
            int ret = doorbell_camera_turn_off();

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_GET_CAMERA_STATUS:
        {

        }
        break;

        case DBCMD_SET_AUDIO_TURN_ON:
        {
            audio_parameters_t parameters;

            STREAM_TO_UINT8(parameters.aec, p);
            STREAM_TO_UINT8(parameters.uac, p);
            STREAM_TO_UINT32(parameters.rmt_recorder_sample_rate, p);
            STREAM_TO_UINT32(parameters.rmt_player_sample_rate, p);
            STREAM_TO_UINT8(parameters.rmt_recoder_fmt, p);
            STREAM_TO_UINT8(parameters.rmt_player_fmt, p);

            int ret = doorbell_audio_turn_on(&parameters);

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_SET_AUDIO_TURN_OFF:
        {
            LOGD("DBCMD_SET_AUDIO_TURN_OFF\n");

            int ret = doorbell_audio_turn_off();

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_GET_AUDIO_STATUS:
        {

        }
        break;

        case DBCMD_SET_LCD_TURN_ON:
        {
            display_parameters_t parameters = {0};
            STREAM_TO_UINT16(parameters.id, p);
            STREAM_TO_UINT16(parameters.rotate_angle, p);
            STREAM_TO_UINT16(parameters.pixel_format, p);

            int ret = doorbell_display_turn_on(&parameters);

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_SET_LCD_TURN_OFF:
        {
            int ret = doorbell_display_turn_off();

            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        case DBCMD_GET_SUPPORTED_LCD_DEVICES:
        {
            doorbell_get_supported_lcd_devices(cmd.opcode);
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
            uint32_t param;
            STREAM_TO_UINT32(param, p);

            int ret = doorbell_audio_acoustics(cmd.param, param);
            doorbell_transmission_event_report(cmd.opcode, ret & 0xFF, EVT_FLAGS_COMPLETE);
        }
        break;

        default:
        {
            doorbell_transmission_event_report(cmd.opcode, EVT_STATUS_UNKNOWN, EVT_FLAGS_COMPLETE);
        }
        break;
    }
}

