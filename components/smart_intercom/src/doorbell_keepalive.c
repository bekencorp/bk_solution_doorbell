#include <os/os.h>
#include <stdlib.h>
#include <components/log.h>
#include <common/bk_include.h>
#include "doorbell_keepalive.h"
#include "doorbell_ipc_msg.h"
#include "doorbell_comm.h"
#include "doorbell_network.h"
#include "doorbell_cmd.h"
#include "doorbell_audio_device.h"
#include <modules/wdrv_common.h>

#define DOORBELL_KEEPALIVE_TAG "DOORBELL_KEEPALIVE"

#define LOGI(...)   BK_LOGI(DOORBELL_KEEPALIVE_TAG, ##__VA_ARGS__)
#define LOGW(...)   BK_LOGW(DOORBELL_KEEPALIVE_TAG, ##__VA_ARGS__)
#define LOGE(...)   BK_LOGE(DOORBELL_KEEPALIVE_TAG, ##__VA_ARGS__)
#define LOGD(...)   BK_LOGD(DOORBELL_KEEPALIVE_TAG, ##__VA_ARGS__)
#define LOGV(...)   BK_LOGV(DOORBELL_KEEPALIVE_TAG, ##__VA_ARGS__)

typedef struct {
    beken2_timer_t mm_status_check_timer;
    bool timer_started;
    /* Keepalive timer interval in ms, configurable via CLI "ka interval <ms>", persisted to flash */
    uint32_t mm_status_check_interval_ms;
    bool interval_loaded_from_flash;
    bool pending_keepalive_after_service_stop;
    uint32_t pending_wakeup_cmd;  // Store the command to send after service starts
    bool device_power_on_notify_sent;
} doorbell_keepalive_env_t;

static doorbell_keepalive_env_t s_keepalive_env = {
    .mm_status_check_interval_ms = MM_STATUS_CHECK_INTERVAL_MS,
};

#define DOORBELL_KEEPALIVE_INTERVAL_MAX_MS (300 * 1000)  /* 5 minutes max */

#define DOORBELL_KEEPALIVE_CLI_CMD_CNT (sizeof(s_doorbell_keepalive_commands) / sizeof(struct cli_command))

static void doorbell_keepalive_mm_status_check_timer_handler(void *larg, void *rarg);

void doorbell_keepalive_cancel_pending_service_stop(void)
{
    if (s_keepalive_env.pending_keepalive_after_service_stop) {
        s_keepalive_env.pending_keepalive_after_service_stop = false;
        LOGI("pending keepalive service stop canceled\n");
    }
}

static void db_set_keepalive_interval(const char *interval_str)
{
    uint32_t interval_ms;
    int err;
    int val;

    if (interval_str == NULL || interval_str[0] == '\0') {
        LOGE("%s: interval string is empty\n", __func__);
        return;
    }

    val = atoi(interval_str);
    if (val <= 0) {
        LOGE("%s: invalid interval: %s (expect positive number, unit: ms)\n", __func__, interval_str);
        return;
    }

    interval_ms = (uint32_t)val;
    if (interval_ms < MM_STATUS_CHECK_MIN_INTERVAL_MS) {
        LOGW("%s: interval %u ms < min %d ms, use min\n", __func__, interval_ms, MM_STATUS_CHECK_MIN_INTERVAL_MS);
        interval_ms = MM_STATUS_CHECK_MIN_INTERVAL_MS;
    }
    if (interval_ms > DOORBELL_KEEPALIVE_INTERVAL_MAX_MS) {
        LOGW("%s: interval %u ms > max %d ms, use max\n", __func__, interval_ms, DOORBELL_KEEPALIVE_INTERVAL_MAX_MS);
        interval_ms = DOORBELL_KEEPALIVE_INTERVAL_MAX_MS;
    }

    s_keepalive_env.mm_status_check_interval_ms = interval_ms;
    LOGI("%s: keepalive interval set to %u ms\n", __func__, s_keepalive_env.mm_status_check_interval_ms);

    if (doorbell_save_keepalive_interval_to_flash(s_keepalive_env.mm_status_check_interval_ms) != BK_OK) {
        LOGW("%s: failed to save interval to flash\n", __func__);
    }

    /* If idle countdown is running, restart it with new interval */
    if (s_keepalive_env.timer_started) {
        err = rtos_oneshot_reload_timer_ex(&s_keepalive_env.mm_status_check_timer,
                                           s_keepalive_env.mm_status_check_interval_ms,
                                           doorbell_keepalive_mm_status_check_timer_handler,
                                           NULL,
                                           NULL);
        if (err != BK_OK) {
            LOGE("%s: Failed to restart idle countdown timer: %d\n", __func__, err);
            return;
        }
        LOGI("%s: Idle countdown timer restarted with interval %u ms\n",
             __func__, s_keepalive_env.mm_status_check_interval_ms);
    }
}

static bk_err_t doorbell_keepalive_stop_service_if_running(void)
{
    bk_err_t ret;
    uint32_t mm_status;
    db_ntwk_service_info_t service_info;
    doorbell_msg_t msg;

    // Check if service stop message has already been sent (indicated by pending keepalive flag)
    if (s_keepalive_env.pending_keepalive_after_service_stop) {
        LOGD("%s: Service stop message already sent, skipping\n", __func__);
        return BK_OK;
    }

    // Read service type from flash
    os_memset(&service_info, 0, sizeof(db_ntwk_service_info_t));
    ret = doorbell_get_ntwk_service_info_from_flash(&service_info);
    if (ret != BK_OK) {
        LOGE("%s: Failed to get service info from flash\n", __func__);
        return BK_FAIL;
    }

    LOGD("%s: Service type from flash: %d\n", __func__, service_info.db_service);

    mm_status = doorbell_mm_service_get_status();
    if (mm_status != 0) {
        LOGI("Multimedia services are active (status: 0x%x), skip service stop\n", mm_status);
        return BK_OK;
    }

    // Stop the service if it's running (TCP or UDP)
    if (service_info.db_service == DOORBELL_SERVICE_LAN_TCP || 
        service_info.db_service == DOORBELL_SERVICE_LAN_UDP) {
        os_memset(&msg, 0, sizeof(doorbell_msg_t));
        msg.param = 0;

        if (service_info.db_service == DOORBELL_SERVICE_LAN_TCP) {
            msg.event = DBEVT_LAN_TCP_SERVICE_STOP;
        } else if (service_info.db_service == DOORBELL_SERVICE_LAN_UDP) {
            msg.event = DBEVT_LAN_UDP_SERVICE_STOP;
        }

        ret = doorbell_send_msg(&msg);
        if (ret != BK_OK) {
            LOGE("%s: Failed to send service stop message\n", __func__);
            return BK_FAIL;
        }

        // Set flag to indicate that service stop message was sent and keepalive should be sent after service stops
        s_keepalive_env.pending_keepalive_after_service_stop = true;
        LOGI("%s: keepalive will be sent after service stops\n", __func__);

        return BK_OK;
    }

    // No service needs to be stopped
    return BK_FAIL;
}


static void doorbell_keepalive_mm_status_check_timer_handler(void *larg, void *rarg)
{
#if CONFIG_DOORBELL_KEEPALIVE_ALWAYS_ON
    LOGI("%s: Always-online mode enabled, skip AP power-down keepalive\n", __func__);
    return;
#endif

    uint32_t mm_status;
    bk_err_t ret;

    // Get multimedia service status
    mm_status = doorbell_mm_service_get_status();
    LOGD("%s: Current multimedia service status: 0x%x\n", __func__, mm_status);

    // Check if there are any active multimedia services
    if (mm_status == 0) {
        LOGI("%s: Idle countdown expired, preparing to send keepalive command\n", __func__);

        // Try to stop service if it's running (TCP or UDP)
        ret = doorbell_keepalive_stop_service_if_running();
        if (ret == BK_OK) {
            // Service stop was sent, or an active multimedia service canceled this keepalive path.
            return;
        }

        s_keepalive_env.pending_keepalive_after_service_stop = true;
    } else {
        LOGD("%s: Multimedia services are active (status: 0x%x), skip keepalive\n",
             __func__, mm_status);
    }
}

// Encapsulated function: Disable Bluetooth
static bk_err_t doorbell_keepalive_disable_bluetooth(void)
{
    doorbell_msg_t msg;
    bk_err_t ret;

    os_memset(&msg, 0, sizeof(doorbell_msg_t));
    msg.event = DBEVT_BLE_DISABLE;
    msg.param = 0;
    ret = doorbell_send_msg(&msg);
    if (ret != BK_OK) {
        LOGE("%s: Failed to send message\n", __func__);
        return BK_FAIL;
    }

    return BK_OK;
}

// Encapsulated function: Start service based on flash configuration
static bk_err_t doorbell_keepalive_start_service_from_flash(void)
{
    bk_err_t ret;
    db_ntwk_service_info_t service_info;
    doorbell_msg_t msg;

    // Read service type from flash
    os_memset(&service_info, 0, sizeof(db_ntwk_service_info_t));
    ret = doorbell_get_ntwk_service_info_from_flash(&service_info);
    if (ret != BK_OK) {
        LOGE("%s: Failed to get service info from flash\n", __func__);
        return BK_FAIL;
    }

    LOGI("%s: Service type from flash: %d\n", __func__, service_info.db_service);

    // Send corresponding service start request message based on service type
    os_memset(&msg, 0, sizeof(doorbell_msg_t));
    msg.param = 0;

    switch (service_info.db_service) {
        case DOORBELL_SERVICE_LAN_UDP:
            msg.event = DBEVT_LAN_UDP_SERVICE_START_REQUEST;
            LOGI("%s: Starting LAN UDP service\n", __func__);
            break;

        case DOORBELL_SERVICE_LAN_TCP:
            msg.event = DBEVT_LAN_TCP_SERVICE_START_REQUEST;
            LOGI("%s: Starting LAN TCP service\n", __func__);
            break;

        case DOORBELL_SERVICE_NONE:
        default:
            LOGW("%s: unknown service type: %d\n", __func__, service_info.db_service);
            return BK_FAIL;
    }

    // Send service start request message
    ret = doorbell_send_msg(&msg);
    if (ret != BK_OK) {
        LOGE("%s: Failed to send service start request message\n", __func__);
        return BK_FAIL;
    }

    return BK_OK;
}

void doorbell_keepalive_on_keepalive_disconnection(void)
{
    bk_err_t ret;

    LOGI("%s: Keepalive establishment failed, re-enabling service\n", __func__);
    s_keepalive_env.pending_keepalive_after_service_stop = false;
    ret = doorbell_keepalive_start_service_from_flash();
    if (ret != BK_OK) {
        LOGE("%s: Failed to start service from flash\n", __func__);
    }
}

// Function to handle service start success and send pending command
void doorbell_keepalive_on_service_start_success(void)
{
    if (s_keepalive_env.pending_wakeup_cmd != 0) {
        /* Wake-up is handled on the CP side (db_pack); after the AP service is
         * back up, the device-power-on notification below tells the server the
         * device is awake. The legacy binary wake-up re-injection is dropped
         * because the control channel is now JSON-RPC framed. */
        LOGI("Service started, pending wakeup cmd %u cleared\n", s_keepalive_env.pending_wakeup_cmd);
        s_keepalive_env.pending_wakeup_cmd = 0;
    }

    if (!s_keepalive_env.device_power_on_notify_sent) {
        LOGI("Service started successfully, notify device power on\n");
        doorbell_transmission_device_power_on_notify();
        s_keepalive_env.device_power_on_notify_sent = true;
    }
}

void doorbell_keepalive_handle_wakeup_reason(void)
{
    bk_err_t ret;
    uint32_t wakeup_reason;

    // Check if pl_wakeup_env is initialized
    if (pl_wakeup_env == NULL) {
        LOGE("%s: pl_wakeup_env is NULL\n", __func__);
        return;
    }

    wakeup_reason = pl_wakeup_env->wakeup_reason;
    LOGI("%s: wakeup_reason = 0x%x\n", __func__, wakeup_reason);

    // Reset pending command
    s_keepalive_env.pending_wakeup_cmd = 0;
    s_keepalive_env.device_power_on_notify_sent = false;

    // Handle different wakeup reasons
    switch (wakeup_reason) {
        case POWERUP_POWER_WAKEUP_FLAG:
            // Normal power-on startup, delete previously stored flash info
            #if 0
            if (doorbell_del_all_info_from_flash() != BK_OK) {
                LOGW("%s: Failed to del all info from flash\n", __func__);
            }
            #endif

            break;

        case POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG:
            // 1. Disable Bluetooth
            ret = doorbell_keepalive_disable_bluetooth();
            if (ret != BK_OK) {
                LOGE("%s: Failed to disable Bluetooth\n", __func__);
                break;
            }

            // 2. Stop keepalive service on CP side
            ret = doorbell_ipc_stop_keepalive();
            if (ret != BK_OK) {
                LOGE("%s: Failed to stop CP keepalive\n", __func__);
                break;
            }

            // 3. Start service from flash
            ret = doorbell_keepalive_start_service_from_flash();
            if (ret != BK_OK) {
                LOGE("%s: Failed to start service from flash\n", __func__);
                break;
            }
            // 4. Store pending command to send after service starts
            s_keepalive_env.pending_wakeup_cmd = DBCMD_WAKE_UP_REQUEST;
            break;

        case POWERUP_KEEPALIVE_DISCONNECTION:

            s_keepalive_env.pending_keepalive_after_service_stop = false;
            ret = doorbell_keepalive_start_service_from_flash();
            if (ret != BK_OK) {
                LOGE("%s: Failed to start service from flash\n", __func__);
            }
            break;

        case POWERUP_KEEPALIVE_FAIL_WAKEUP_FLAG:
            ret = doorbell_keepalive_disable_bluetooth();
            if (ret != BK_OK) {
                LOGE("%s: Failed to disable Bluetooth\n", __func__);
                break;
            }

            s_keepalive_env.pending_keepalive_after_service_stop = false;
            ret = doorbell_keepalive_start_service_from_flash();
            if (ret != BK_OK) {
                LOGE("%s: Failed to start service from flash\n", __func__);
            }
            break;

        default:
            // Invalid or unknown wakeup reason
            LOGW("%s: Invalid or unknown wakeup reason: 0x%x\n", __func__, wakeup_reason);
            break;
    }
}

bk_err_t doorbell_keepalive_start_mm_status_check(void)
{
#if CONFIG_DOORBELL_KEEPALIVE_ALWAYS_ON
    LOGI("%s: Always-online mode enabled, skip idle countdown\n", __func__);
    return BK_OK;
#endif

    int err;
    uint32_t flash_interval;
    uint32_t mm_status;

    /* Load interval from flash on first start (after power-on) */
    if (!s_keepalive_env.interval_loaded_from_flash) {
        s_keepalive_env.interval_loaded_from_flash = true;
        if (doorbell_get_keepalive_interval_from_flash(&flash_interval) == BK_OK &&
            flash_interval >= MM_STATUS_CHECK_MIN_INTERVAL_MS &&
            flash_interval <= DOORBELL_KEEPALIVE_INTERVAL_MAX_MS) {
            s_keepalive_env.mm_status_check_interval_ms = flash_interval;
            LOGI("%s: using keepalive interval from flash: %u ms\n", __func__, s_keepalive_env.mm_status_check_interval_ms);
        }
    }

    mm_status = doorbell_mm_service_get_status();
    if (mm_status != 0) {
        LOGD("%s: Multimedia services are active (status: 0x%x), skip idle countdown\n",
             __func__, mm_status);
        return BK_OK;
    }

    if (s_keepalive_env.timer_started) {
        if (rtos_is_oneshot_timer_running(&s_keepalive_env.mm_status_check_timer)) {
            LOGD("%s: Idle countdown timer already running\n", __func__);
            return BK_OK;
        }

        err = rtos_oneshot_reload_timer_ex(&s_keepalive_env.mm_status_check_timer,
                                           s_keepalive_env.mm_status_check_interval_ms,
                                           doorbell_keepalive_mm_status_check_timer_handler,
                                           NULL,
                                           NULL);
        if (err != BK_OK) {
            LOGE("%s: Failed to reload idle countdown timer: %d\n", __func__, err);
            return BK_FAIL;
        }

        LOGI("%s: Idle countdown timer reloaded (interval: %u ms)\n",
             __func__, s_keepalive_env.mm_status_check_interval_ms);
        return BK_OK;
    }

    // Initialize one-shot timer with current interval (from flash or default)
    err = rtos_init_oneshot_timer(&s_keepalive_env.mm_status_check_timer,
                                  s_keepalive_env.mm_status_check_interval_ms,
                                  doorbell_keepalive_mm_status_check_timer_handler,
                                  NULL,
                                  NULL);
    if (err != BK_OK) {
        LOGE("%s: Failed to init timer: %d\n", __func__, err);
        return BK_FAIL;
    }

    // Start idle countdown timer
    err = rtos_start_oneshot_timer(&s_keepalive_env.mm_status_check_timer);
    if (err != BK_OK) {
        LOGE("%s: Failed to start timer: %d\n", __func__, err);
        rtos_deinit_oneshot_timer(&s_keepalive_env.mm_status_check_timer);
        return BK_FAIL;
    }

    s_keepalive_env.timer_started = true;
    LOGI("%s: Idle countdown timer started (interval: %u ms)\n",
         __func__, s_keepalive_env.mm_status_check_interval_ms);

    return BK_OK;
}

void doorbell_keepalive_send_keepalive(void)
{
#if CONFIG_DOORBELL_KEEPALIVE_ALWAYS_ON
    s_keepalive_env.pending_keepalive_after_service_stop = false;
    LOGI("%s: Always-online mode enabled, skip AP power-down keepalive\n", __func__);
    return;
#endif

    bk_err_t ret = BK_OK;
    uint32_t mm_status;
    ntwk_server_net_info_t net_info;

    // Check if there's a pending keepalive request
    if (!s_keepalive_env.pending_keepalive_after_service_stop) {
        LOGW("%s: No pending keepalive request, returning\n", __func__);
        return;
    }

    mm_status = doorbell_mm_service_get_status();
    if (mm_status != 0) {
        s_keepalive_env.pending_keepalive_after_service_stop = false;
        LOGI("%s: Multimedia services are active (status: 0x%x), skip keepalive\n",
             __func__, mm_status);
        return;
    }

    // Clear the flag first
    s_keepalive_env.pending_keepalive_after_service_stop = false;

    // Read network information from flash
    os_memset(&net_info, 0, sizeof(ntwk_server_net_info_t));
    ret = doorbell_get_server_net_info_from_flash(&net_info);
    if (ret != BK_OK) {
        LOGE("%s: Failed to get server net info from flash\n", __func__);
        return;
    }

    // Check if IP address and port are valid
    if (net_info.ip_addr[0] == '\0' || net_info.cmd_port[0] == '\0') {
        LOGW("%s: Invalid network info (IP or port is empty)\n", __func__);
        return;
    }

#if (CONFIG_ASR_SERVICE_WITH_MIC)
    /* About to power off AP for keepalive: free the mic/ADC. */
    doorbell_asr_turn_off();
#endif

    // Send keepalive command with IP address and cmd_port
    ret = doorbell_ipc_start_keepalive((const char *)net_info.ip_addr, (const char *)net_info.cmd_port);
    if (ret != BK_OK) {
        LOGE("%s: Failed to send keepalive command\n", __func__);
        return;
    }

    doorbell_keepalive_stop_mm_status_check();
}

bk_err_t doorbell_keepalive_stop_mm_status_check(void)
{
    int err;

    if (!s_keepalive_env.timer_started) {
        LOGD("%s: Idle countdown timer not started\n", __func__);
        return BK_OK;
    }

    // Stop timer if it is still counting down
    if (rtos_is_oneshot_timer_running(&s_keepalive_env.mm_status_check_timer)) {
        err = rtos_stop_oneshot_timer(&s_keepalive_env.mm_status_check_timer);
        if (err != BK_OK) {
            LOGE("%s: Failed to stop timer: %d\n", __func__, err);
        }
    }

    // Deinitialize timer
    err = rtos_deinit_oneshot_timer(&s_keepalive_env.mm_status_check_timer);
    if (err != BK_OK) {
        LOGE("%s: Failed to deinit timer: %d\n", __func__, err);
    }

    s_keepalive_env.timer_started = false;
    s_keepalive_env.mm_status_check_timer.handle = NULL;
    LOGI("%s: Idle countdown timer stopped\n", __func__);

    return BK_OK;
}


static void doorbell_keepalive_cli_help(void)
{
    BK_LOG_RAW("ka <arg1> <arg2> ...\r\n");
    BK_LOG_RAW("-----------------------ka COMMAND---------------------------------\r\n");
    BK_LOG_RAW("ka                                      - help infomation\r\n");
    BK_LOG_RAW("ka interval                             - set keepalive interval\r\n");
}

static void doorbell_keepalive_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc <= 2) {
        doorbell_keepalive_cli_help();
        return;
    }

    if ((os_strcmp(argv[1], "interval") == 0))
    {
        LOGI("%s: Setting keepalive interval to %s\n", __func__, argv[2]);
        db_set_keepalive_interval(argv[2]);
    }
    else
    {
        LOGW("%s: Unknown command: %s\n", __func__, argv[1]);
        doorbell_keepalive_cli_help();
    }
}

static const struct cli_command s_doorbell_keepalive_commands[] = {
	{"ka", "ka CLI commands", doorbell_keepalive_cli_cmd},
};

int doorbell_keepalive_cli_init(void)
{
	return cli_register_commands(s_doorbell_keepalive_commands, DOORBELL_KEEPALIVE_CLI_CMD_CNT);
}
