/*
 * doorbell_lp bidirectional video-intercom on-device self-test CLI.
 *
 * Provides "db_selftest" to exercise the JSON-RPC control plane and the
 * downlink H.264 decode + ISP PIP display path without a companion APK:
 *   - db_selftest rpc <preset>|raw {json}  : inject JSON-RPC into the engine
 *   - db_selftest cam on|off               : ISP camera + encoder + uplink task
 *   - db_selftest lcd on|off               : panel on/off
 *   - db_selftest downlink on [loops]|off  : uplink->downlink loopback feed
 */

#include <common/bk_include.h>
#include <os/os.h>
#include <os/str.h>
#include <components/log.h>
#include <stdio.h>
#include <stdlib.h>

#include "cli.h"

#include "doorbell_jsonrpc.h"
#include "doorbell_downlink_video.h"
#include "doorbell_devices.h"
#include "doorbell_devices_intercom.h"
#include "doorbell_display_compositor.h"
#include "doorbell_selftest.h"

#define TAG "db-selftest"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* ISP MP encode geometry (see ap_main.c camera_board.isp.mp_*): the downlink
 * decoder must be configured to the same size for the loopback to decode. */
#define DB_ST_LOOPBACK_WIDTH  1280
#define DB_ST_LOOPBACK_HEIGHT 720
#define DB_ST_LOOPBACK_FPS    15

#define DB_ST_RAW_JSON_MAX    1024

/* ---- downlink loopback tee state (written by CLI, read by uplink task) ---- */
static volatile bool     s_tee_enabled = false;
static volatile uint32_t s_tee_loops   = 0; /* 0 = feed forever until "off" */
static volatile uint32_t s_tee_fed     = 0;

/* Ask the uplink encoder for a fresh IDR this often (in fed frames) so the
 * downlink decoder can re-establish a keyframe reference even if it armed
 * mid-GOP or lost its reference chain under transient GPU/PIP contention. */
#define DB_ST_IDR_REFRESH_FRAMES 30U

void doorbell_selftest_downlink_tee_feed(uint8_t *data, uint32_t len)
{
    if (!s_tee_enabled || data == NULL || len == 0)
    {
        return;
    }

    (void)doorbell_downlink_video_recv(data, len);
    s_tee_fed++;

    /* Periodic keyframe refresh keeps the loopback self-recovering. */
    if ((s_tee_fed % DB_ST_IDR_REFRESH_FRAMES) == 0U)
    {
        (void)doorbell_devices_force_idr();
    }

    if (s_tee_loops != 0 && s_tee_fed >= s_tee_loops)
    {
        s_tee_enabled = false;
        LOGI("downlink loopback fed %u frames, tee auto-off\n", (unsigned)s_tee_fed);
    }
}

/* ---- JSON-RPC presets --------------------------------------------------- */
typedef struct
{
    const char *name;
    const char *json;
} db_st_preset_t;

static const db_st_preset_t s_rpc_presets[] =
{
    { "ping",        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"doorbell.misc.ping\"}" },
    { "getconfig",   "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"doorbell.solution.getConfig\"}" },
    { "badmethod",   "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"doorbell.no.such.method\"}" },
    /* Deliberately malformed JSON to trigger -32700 parse error. */
    { "badjson",     "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":" },
    /* camera.turnOn with no params -> -32602 invalid params. */
    { "noparams",    "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"doorbell.camera.turnOn\"}" },
    /* Unknown-method notification (no id) -> silently dropped, no TX. */
    { "notify",      "{\"jsonrpc\":\"2.0\",\"method\":\"doorbell.no.such.method\"}" },
    { "camstatus",   "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"doorbell.camera.getStatus\"}" },
    { "lcdstatus",   "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"doorbell.lcd.getStatus\"}" },
    { "audiostatus", "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"doorbell.audio.getStatus\"}" },
    { "setacoustics","{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"doorbell.audio.setAcoustics\",\"params\":{\"name\":\"echoDepth\",\"value\":50}}" },
    { "servicetype", "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"doorbell.service.setType\",\"params\":{\"serviceType\":\"udp\"}}" },
};

/* camera/lcd/downlink helper presets (built via the RPC engine so the whole
 * dispatch + handler path is exercised). */
static const char s_cam_on_json[] =
    "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"doorbell.camera.turnOn\",\"params\":"
    "{\"streamCount\":1,\"streams\":[{\"cameraType\":\"mipi\",\"cameraConfig\":"
    "{\"mipi\":{\"width\":1280,\"height\":720,\"fps\":15,\"videoFormat\":\"h264\",\"sensorId\":1}}}]}}";

static const char s_cam_off_json[] =
    "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"doorbell.camera.turnOff\",\"params\":{\"target\":\"all\"}}";

static const char s_lcd_on_json[] =
    "{\"jsonrpc\":\"2.0\",\"id\":22,\"method\":\"doorbell.lcd.turnOn\",\"params\":"
    "{\"lcdType\":\"mipi\",\"lcdConfig\":{\"mipi\":{}}}}";

static const char s_lcd_off_json[] =
    "{\"jsonrpc\":\"2.0\",\"id\":23,\"method\":\"doorbell.lcd.turnOff\",\"params\":{}}";

/* width/height must match ISP MP (ap_main.c) = the actual uplink encode size. */
static const char s_downlink_cfg_json[] =
    "{\"jsonrpc\":\"2.0\",\"id\":24,\"method\":\"doorbell.imageStream.setReceiveConfig\",\"params\":"
    "{\"imageFormat\":\"h264\",\"formatConfig\":{\"h264\":{\"width\":1280,\"height\":720,\"fps\":15,\"pFrameCount\":29}}}}";

static void db_st_inject(const char *json)
{
    LOGI("inject: %s\n", json);
    doorbell_jsonrpc_handle_cmd(json, (uint32_t)os_strlen(json));
}

static void db_st_help(void)
{
    BK_LOG_RAW("db_selftest <sub> [args]\r\n");
    BK_LOG_RAW("-------------------- db_selftest COMMANDS --------------------\r\n");
    BK_LOG_RAW("db_selftest help                 - this help\r\n");
    BK_LOG_RAW("db_selftest rpc <preset>         - inject a preset JSON-RPC\r\n");
    BK_LOG_RAW("    presets: ping getconfig badmethod badjson noparams notify\r\n");
    BK_LOG_RAW("             camstatus lcdstatus audiostatus setacoustics servicetype\r\n");
    BK_LOG_RAW("db_selftest rpc raw {json}       - inject a custom JSON-RPC\r\n");
    BK_LOG_RAW("db_selftest cam on|off           - ISP camera + encoder + uplink\r\n");
    BK_LOG_RAW("db_selftest lcd on|off           - panel on/off\r\n");
    BK_LOG_RAW("db_selftest downlink on [loops]  - loopback uplink AU -> downlink decode\r\n");
    BK_LOG_RAW("db_selftest downlink off         - stop loopback + downlink pipeline\r\n");
}

static void db_st_cmd_rpc(int argc, char **argv)
{
    uint32_t i;

    if (argc < 3)
    {
        LOGW("usage: db_selftest rpc <preset>|raw {json}\n");
        return;
    }

    if (os_strcmp(argv[2], "raw") == 0)
    {
        static char raw[DB_ST_RAW_JSON_MAX];
        int pos = 0;
        int j;

        if (argc < 4)
        {
            LOGW("usage: db_selftest rpc raw {json}\n");
            return;
        }

        raw[0] = '\0';
        for (j = 3; j < argc; j++)
        {
            int n = snprintf(raw + pos, sizeof(raw) - pos, "%s%s",
                             (j > 3) ? " " : "", argv[j]);
            if (n <= 0 || (uint32_t)(pos + n) >= sizeof(raw))
            {
                LOGW("raw json too long (>%d)\n", DB_ST_RAW_JSON_MAX);
                return;
            }
            pos += n;
        }
        db_st_inject(raw);
        return;
    }

    for (i = 0; i < sizeof(s_rpc_presets) / sizeof(s_rpc_presets[0]); i++)
    {
        if (os_strcmp(argv[2], s_rpc_presets[i].name) == 0)
        {
            db_st_inject(s_rpc_presets[i].json);
            return;
        }
    }

    LOGW("unknown preset: %s (see 'db_selftest help')\n", argv[2]);
}

static void db_st_cmd_cam(int argc, char **argv)
{
    if (argc < 3)
    {
        LOGW("usage: db_selftest cam on|off\n");
        return;
    }
    if (os_strcmp(argv[2], "on") == 0)
    {
        db_st_inject(s_cam_on_json);
    }
    else if (os_strcmp(argv[2], "off") == 0)
    {
        db_st_inject(s_cam_off_json);
    }
    else
    {
        LOGW("usage: db_selftest cam on|off\n");
    }
}

static void db_st_cmd_lcd(int argc, char **argv)
{
    if (argc < 3)
    {
        LOGW("usage: db_selftest lcd on|off\n");
        return;
    }
    if (os_strcmp(argv[2], "on") == 0)
    {
        db_st_inject(s_lcd_on_json);
    }
    else if (os_strcmp(argv[2], "off") == 0)
    {
        db_st_inject(s_lcd_off_json);
    }
    else
    {
        LOGW("usage: db_selftest lcd on|off\n");
    }
}

static void db_st_cmd_downlink(int argc, char **argv)
{
    if (argc < 3)
    {
        LOGW("usage: db_selftest downlink on [loops]|off\n");
        return;
    }

    if (os_strcmp(argv[2], "on") == 0)
    {
        uint32_t loops = 0;

        if (argc >= 4)
        {
            int v = atoi(argv[3]);
            loops = (v > 0) ? (uint32_t)v : 0;
        }

        if (doorbell_devices_isp_handle_get() == NULL)
        {
            LOGW("ISP not running: run 'db_selftest cam on' first (PIP will be off)\n");
        }

        if (!doorbell_downlink_video_is_running())
        {
            db_st_inject(s_downlink_cfg_json);
            if (!doorbell_downlink_video_is_running())
            {
                LOGE("downlink pipeline failed to start, abort loopback\n");
                return;
            }
        }

        s_tee_fed = 0;
        s_tee_loops = loops;
        /* Force a keyframe now so the very first fed AU is a decodable IDR;
         * the encoder may have been running (P-frames only) long before this. */
        (void)doorbell_devices_force_idr();
        s_tee_enabled = true;
        LOGI("downlink loopback armed (loops=%u, 0=forever)\n", (unsigned)loops);
    }
    else if (os_strcmp(argv[2], "off") == 0)
    {
        s_tee_enabled = false;
        (void)doorbell_downlink_video_stop();
        LOGI("downlink loopback + pipeline stopped (fed=%u)\n", (unsigned)s_tee_fed);
    }
    else
    {
        LOGW("usage: db_selftest downlink on [loops]|off\n");
    }
}

static void db_st_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2 || os_strcmp(argv[1], "help") == 0)
    {
        db_st_help();
        return;
    }

    if (os_strcmp(argv[1], "rpc") == 0)
    {
        db_st_cmd_rpc(argc, argv);
    }
    else if (os_strcmp(argv[1], "cam") == 0)
    {
        db_st_cmd_cam(argc, argv);
    }
    else if (os_strcmp(argv[1], "lcd") == 0)
    {
        db_st_cmd_lcd(argc, argv);
    }
    else if (os_strcmp(argv[1], "downlink") == 0)
    {
        db_st_cmd_downlink(argc, argv);
    }
    else
    {
        LOGW("unknown sub: %s\n", argv[1]);
        db_st_help();
    }
}

static const struct cli_command s_doorbell_selftest_commands[] = {
    {"db_selftest", "doorbell bidir video-intercom self-test", db_st_cli_cmd},
};

int doorbell_selftest_cli_init(void)
{
    return cli_register_commands(s_doorbell_selftest_commands,
                                 sizeof(s_doorbell_selftest_commands) / sizeof(struct cli_command));
}
