#include <os/os.h>
#include <stdlib.h>
#include <components/log.h>
#include <common/bk_include.h>
#include "db_keepalive.h"
#include "db_ipc_msg/db_ipc_msg.h"
#include "doorbell_comm.h"
#include "doorbell_network.h"
#include "doorbell_cmd.h"
#include <modules/wdrv_common.h>

#define DB_KEEPALIVE_TAG "DB_KEEPALIVE"

#define LOGI(...)   BK_LOGI(DB_KEEPALIVE_TAG, ##__VA_ARGS__)
#define LOGW(...)   BK_LOGW(DB_KEEPALIVE_TAG, ##__VA_ARGS__)
#define LOGE(...)   BK_LOGE(DB_KEEPALIVE_TAG, ##__VA_ARGS__)
#define LOGD(...)   BK_LOGD(DB_KEEPALIVE_TAG, ##__VA_ARGS__)
#define LOGV(...)   BK_LOGV(DB_KEEPALIVE_TAG, ##__VA_ARGS__)

static beken_timer_t s_mm_status_check_timer = {0};
static bool s_timer_started = false;
/* Keepalive timer interval in ms, configurable via CLI "ka interval <ms>", persisted to flash */
static uint32_t s_mm_status_check_interval_ms = MM_STATUS_CHECK_INTERVAL_MS;
static bool s_interval_loaded_from_flash = false;

static uint64_t s_last_update_timestamp = 0;
static bool s_pending_keepalive_after_service_stop = false;

static uint32_t s_pending_wakeup_cmd = 0;  // Store the command to send after service starts

#define DB_KEEPALIVE_INTERVAL_MAX_MS (300 * 1000)  /* 5 minutes max */

#define DB_KEEPALIVE_CLI_CMD_CNT (sizeof(s_db_keepalive_commands) / sizeof(struct cli_command))

static void db_keepalive_mm_status_check_timer_handler(void *data);

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
    if (interval_ms > DB_KEEPALIVE_INTERVAL_MAX_MS) {
        LOGW("%s: interval %u ms > max %d ms, use max\n", __func__, interval_ms, DB_KEEPALIVE_INTERVAL_MAX_MS);
        interval_ms = DB_KEEPALIVE_INTERVAL_MAX_MS;
    }

    s_mm_status_check_interval_ms = interval_ms;
    LOGI("%s: keepalive interval set to %u ms\n", __func__, s_mm_status_check_interval_ms);

    if (doorbell_save_keepalive_interval_to_flash(s_mm_status_check_interval_ms) != BK_OK) {
        LOGW("%s: failed to save interval to flash\n", __func__);
    }

    /* If timer is running, restart it with new interval */
    if (s_timer_started) {
        err = rtos_stop_timer(&s_mm_status_check_timer);
        if (err != BK_OK) {
            LOGE("%s: Failed to stop timer\n", __func__);
            return;
        }
        err = rtos_deinit_timer(&s_mm_status_check_timer);
        if (err != BK_OK) {
            LOGE("%s: Failed to deinit timer\n", __func__);
            return;
        }
        s_timer_started = false;

        err = rtos_init_timer(&s_mm_status_check_timer,
                              s_mm_status_check_interval_ms,
                              db_keepalive_mm_status_check_timer_handler,
                              NULL);
        if (err != BK_OK) {
            LOGE("%s: Failed to re-init timer: %d\n", __func__, err);
            return;
        }
        err = rtos_start_timer(&s_mm_status_check_timer);
        if (err != BK_OK) {
            LOGE("%s: Failed to restart timer: %d\n", __func__, err);
            rtos_deinit_timer(&s_mm_status_check_timer);
            return;
        }
        s_timer_started = true;
        LOGI("%s: Timer restarted with interval %u ms\n", __func__, s_mm_status_check_interval_ms);
    }
}

static bk_err_t db_keepalive_stop_service_if_running(void)
{
    bk_err_t ret;
    db_ntwk_service_info_t service_info;
    doorbell_msg_t msg;

    // Check if service stop message has already been sent (indicated by pending keepalive flag)
    if (s_pending_keepalive_after_service_stop) {
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
        s_pending_keepalive_after_service_stop = true;
        LOGI("%s: keepalive will be sent after service stops\n", __func__);

        return BK_OK;
    }

    // No service needs to be stopped
    return BK_FAIL;
}


static void db_keepalive_mm_status_check_timer_handler(void *data)
{
    uint32_t mm_status;
    bk_err_t ret;
    uint64_t current_time;
    uint64_t time_diff;

    // Get multimedia service status
    mm_status = doorbell_mm_service_get_status();
    LOGD("%s: Current multimedia service status: 0x%x\n", __func__, mm_status);

    // Check if there are any active multimedia services
    if (mm_status == 0) {
        // Get current time
        current_time = rtos_get_time();
        
        // Check if last update timestamp is valid (non-zero)
        if (s_last_update_timestamp != 0) {
            // Calculate time difference
            if (current_time >= s_last_update_timestamp) {
                time_diff = current_time - s_last_update_timestamp;
            } else {
                // Handle time wrap-around
                time_diff = (UINT64_MAX - s_last_update_timestamp) + current_time + 1;
            }

            LOGD("%s: Time since last update: %llu ms\n", __func__, (unsigned long long)time_diff);

            if (time_diff < MM_STATUS_CHECK_MIN_INTERVAL_MS) {
                LOGI("%s: Last update was %llu ms ago (< %d ms), skipping keepalive\n",
                     __func__, (unsigned long long)time_diff, MM_STATUS_CHECK_MIN_INTERVAL_MS);
                return;
            }
        }

        LOGI("%s: No active multimedia services, preparing to send keepalive command\n", __func__);

        // Try to stop service if it's running (TCP or UDP)
        ret = db_keepalive_stop_service_if_running();
        if (ret == BK_OK) {
            // Service stop message was sent, keepalive will be sent after service stops
            return;
        }

        s_pending_keepalive_after_service_stop = true;
    } else {
        LOGD("%s: Multimedia services are active (status: 0x%x), skip keepalive\n", 
             __func__, mm_status);
    }
}

// Encapsulated function: Disable Bluetooth
static bk_err_t db_keepalive_disable_bluetooth(void)
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
static bk_err_t db_keepalive_start_service_from_flash(void)
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

static bk_err_t db_keepalive_send_cmd_to_callback(uint32_t cmd_opcode, uint32_t param, uint16_t length, uint8_t *payload)
{
    db_cmd_head_t *cmd_ptr;
    uint8_t *send_buf;
    uint16_t total_len;

    total_len = sizeof(db_cmd_head_t) + length;
    send_buf = (uint8_t *)os_malloc(total_len);
    if (send_buf == NULL) {
        LOGE("%s: Failed to allocate memory for command\n", __func__);
        return BK_FAIL;
    }

    cmd_ptr = (db_cmd_head_t *)send_buf;

    cmd_ptr->opcode = CHECK_ENDIAN_UINT32(cmd_opcode);
    cmd_ptr->param = CHECK_ENDIAN_UINT32(param);
    cmd_ptr->length = CHECK_ENDIAN_UINT16(length);

    // Copy payload if any
    if (length > 0 && payload != NULL) {
        os_memcpy(cmd_ptr->payload, payload, length);
    }

    LOGI("%s: opcode=%u\n", __func__, cmd_opcode);
    doorbell_transmission_cmd_recive_callback(send_buf, total_len);
    
    os_free(send_buf);

    LOGI("%s: Command opcode=%u processed successfully\n", __func__, cmd_opcode);
    return BK_OK;
}

void db_keepalive_on_keepalive_disconnection(void)
{
    bk_err_t ret;

    LOGI("%s: Keepalive establishment failed, re-enabling service\n", __func__);
    s_pending_keepalive_after_service_stop = false;
    ret = db_keepalive_start_service_from_flash();
    if (ret != BK_OK) {
        LOGE("%s: Failed to start service from flash\n", __func__);
    }
}

// Function to handle service start success and send pending command
void db_keepalive_on_service_start_success(void)
{
    if (s_pending_wakeup_cmd != 0) {
        LOGI("%s: Service started successfully, sending pending command: %u\n", 
             __func__, s_pending_wakeup_cmd);
        
        switch (s_pending_wakeup_cmd) {
            case DBCMD_WAKE_UP_REQUEST:
                db_keepalive_send_cmd_to_callback(DBCMD_WAKE_UP_REQUEST, 0, 0, NULL);
                break;
            default:
                LOGW("%s: Unknown pending command: %u\n", __func__, s_pending_wakeup_cmd);
                break;
        }
        
        s_pending_wakeup_cmd = 0;
    }
}

void db_keepalive_handle_wakeup_reason(void)
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
    s_pending_wakeup_cmd = 0;

    // Handle different wakeup reasons
    switch (wakeup_reason) {
        case POWERUP_POWER_WAKEUP_FLAG:
            // Normal power-on startup, no special operation needed
            LOGI("%s: Normal power-on startup\n", __func__);
            break;

        case POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG:
            LOGI("%s: Wake up request detected\n", __func__);

            // 1. Disable Bluetooth
            ret = db_keepalive_disable_bluetooth();
            if (ret != BK_OK) {
                LOGE("%s: Failed to disable Bluetooth\n", __func__);
                break;
            }

            // 2. Stop keepalive service on CP side
            ret = db_ipc_stop_keepalive();
            if (ret != BK_OK) {
                LOGE("%s: Failed to stop CP keepalive\n", __func__);
                break;
            }

            // 3. Start service from flash
            ret = db_keepalive_start_service_from_flash();
            if (ret != BK_OK) {
                LOGE("%s: Failed to start service from flash\n", __func__);
                break;
            }
            // 4. Store pending command to send after service starts
            s_pending_wakeup_cmd = DBCMD_WAKE_UP_REQUEST;
            LOGI("%s: Will send DBCMD_WAKE_UP_REQUEST after service starts\n", __func__);
            break;

        case POWERUP_KEEPALIVE_DISCONNECTION:

            LOGI("%s: Keepalive establishment failed, re-enabling service\n", __func__);
            s_pending_keepalive_after_service_stop = false;
            ret = db_keepalive_start_service_from_flash();
            if (ret != BK_OK) {
                LOGE("%s: Failed to start service from flash\n", __func__);
            }
            break;

        case POWERUP_KEEPALIVE_FAIL_WAKEUP_FLAG:
            LOGI("%s: Keepalive failure wakeup, disable BT and start service\n", __func__);
            ret = db_keepalive_disable_bluetooth();
            if (ret != BK_OK) {
                LOGE("%s: Failed to disable Bluetooth\n", __func__);
                break;
            }

            s_pending_keepalive_after_service_stop = false;
            ret = db_keepalive_start_service_from_flash();
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

bk_err_t db_keepalive_start_mm_status_check(void)
{
    int err;
    uint32_t flash_interval;

    if (s_timer_started) {
        LOGW("%s: Timer already started\n", __func__);
        return BK_OK;
    }

    /* Load interval from flash on first start (after power-on) */
    if (!s_interval_loaded_from_flash) {
        s_interval_loaded_from_flash = true;
        if (doorbell_get_keepalive_interval_from_flash(&flash_interval) == BK_OK &&
            flash_interval >= MM_STATUS_CHECK_MIN_INTERVAL_MS &&
            flash_interval <= DB_KEEPALIVE_INTERVAL_MAX_MS) {
            s_mm_status_check_interval_ms = flash_interval;
            LOGI("%s: using keepalive interval from flash: %u ms\n", __func__, s_mm_status_check_interval_ms);
        }
    }

    // Initialize timer with current interval (from flash or default)
    err = rtos_init_timer(&s_mm_status_check_timer,
                          s_mm_status_check_interval_ms,
                          db_keepalive_mm_status_check_timer_handler,
                          NULL);
    if (err != BK_OK) {
        LOGE("%s: Failed to init timer: %d\n", __func__, err);
        return BK_FAIL;
    }

    // Start timer
    err = rtos_start_timer(&s_mm_status_check_timer);
    if (err != BK_OK) {
        LOGE("%s: Failed to start timer: %d\n", __func__, err);
        rtos_deinit_timer(&s_mm_status_check_timer);
        return BK_FAIL;
    }

    s_timer_started = true;
    LOGI("%s: Multimedia status check timer started (interval: %u ms)\n",
         __func__, s_mm_status_check_interval_ms);

    return BK_OK;
}

void db_keepalive_update_timestamp(void)
{
    s_last_update_timestamp = rtos_get_time();
    LOGD("%s: Updated timestamp to %llu ms\n", __func__, (unsigned long long)s_last_update_timestamp);
}

void db_keepalive_send_keepalive(void)
{
    bk_err_t ret = BK_OK;
    ntwk_server_net_info_t net_info;

    // Check if there's a pending keepalive request
    if (!s_pending_keepalive_after_service_stop) {
        LOGW("%s: No pending keepalive request, returning\n", __func__);
        return;
    }

    // Clear the flag first
    s_pending_keepalive_after_service_stop = false;

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

    // Send keepalive command with IP address and cmd_port
    ret = db_ipc_start_keepalive((const char *)net_info.ip_addr, (const char *)net_info.cmd_port);
    if (ret != BK_OK) {
        LOGE("%s: Failed to send keepalive command\n", __func__);
        return;
    }

    db_keepalive_stop_mm_status_check();
}

bk_err_t db_keepalive_stop_mm_status_check(void)
{
    int err;

    if (!s_timer_started) {
        LOGW("%s: Timer not started\n", __func__);
        return BK_OK;
    }

    // Stop timer
    err = rtos_stop_timer(&s_mm_status_check_timer);
    if (err != BK_OK) {
        LOGE("%s: Failed to stop timer: %d\n", __func__, err);
    }

    // Deinitialize timer
    err = rtos_deinit_timer(&s_mm_status_check_timer);
    if (err != BK_OK) {
        LOGE("%s: Failed to deinit timer: %d\n", __func__, err);
    }

    s_timer_started = false;
    s_mm_status_check_timer.handle = NULL;
    LOGI("%s: Multimedia status check timer stopped\n", __func__);

    return BK_OK;
}


static void db_keepalive_cli_help(void)
{
    BK_LOG_RAW("ka <arg1> <arg2> ...\r\n");
    BK_LOG_RAW("-----------------------ka COMMAND---------------------------------\r\n");
    BK_LOG_RAW("ka                                      - help infomation\r\n");
    BK_LOG_RAW("ka interval                             - set keepalive interval\r\n");
}

static void db_keepalive_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc <= 2) {
        db_keepalive_cli_help();
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
        db_keepalive_cli_help();
    }
}

static const struct cli_command s_db_keepalive_commands[] = {
	{"ka", "ka CLI commands", db_keepalive_cli_cmd},
};

int db_keepalive_cli_init(void)
{
	return cli_register_commands(s_db_keepalive_commands, DB_KEEPALIVE_CLI_CMD_CNT);
}
