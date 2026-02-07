#include "db_keepalive.h"
#include "db_pack.h"
#include "db_ipc_msg.h"
#include <os/mem.h>
#include <os/str.h>
#include <driver/pwr_clk.h>
#include "modules/cif_common.h"
#include "powerctrl.h"
#include <errno.h>
#include <string.h>
#include <driver/aon_rtc_types.h>
#include <driver/aon_rtc.h>
#include "bk_pm_internal_api.h"

#define DBK_TAG "DBK_CP"

#define LOGI(...)   BK_LOGI(DBK_TAG, ##__VA_ARGS__)
#define LOGW(...)   BK_LOGW(DBK_TAG, ##__VA_ARGS__)
#define LOGE(...)   BK_LOGE(DBK_TAG, ##__VA_ARGS__)
#define LOGD(...)   BK_LOGD(DBK_TAG, ##__VA_ARGS__)
#define LOGV(...)   BK_LOGV(DBK_TAG, ##__VA_ARGS__)

static db_keepalive_env_t s_keepalive_env = {0};
static bool s_host_powered_down = false;
static bool s_notify_ap_keepalive_failed = false;

// Print packed data content in hex format
static void db_keepalive_print_packed_data(uint8_t *pack_ptr, uint32_t pack_len)
{
    uint32_t i;
    uint32_t print_len = (pack_len > 64) ? 64 : pack_len;  // Limit to 64 bytes for readability
    
    LOGD("%s: Packed data length=%u, content (first %u bytes):\n", __func__, pack_len, print_len);
    for (i = 0; i < print_len; i++) {
        if (i % 16 == 0) {
            BK_LOG_RAW(" \r\n");
        }
        BK_LOG_RAW("%02X ", pack_ptr[i]);
    }

    if (pack_len > print_len) {
        BK_LOG_RAW("... (truncated, total %u bytes)", pack_len);
    }
    BK_LOG_RAW("\n");
}

void doorbell_transmission_event_report(uint32_t opcode, uint8_t status, uint16_t flags)
{
    int ret;
    db_evt_head_t evt;
    uint8_t *pack_ptr;
    uint32_t pack_len;

    LOGD("%s, opcode=%u, status=%u, flags=%u\n", __func__, opcode, status, flags);

    // Check if keepalive socket is valid
    if (s_keepalive_env.sock < 0) {
        LOGE("%s: Keepalive socket is invalid, cannot send event\n", __func__);
        return;
    }

    // Prepare event message
    evt.opcode = CHECK_ENDIAN_UINT16(opcode);
    evt.status = status;
    evt.length = CHECK_ENDIAN_UINT16(0);
    evt.flags = flags;

    // Pack the event with db_pack header
    ret = db_pack_pack((uint8_t *)&evt, sizeof(db_evt_head_t), &pack_ptr, &pack_len);
    if (ret < 0) {
        LOGE("%s: Failed to pack event\n", __func__);
        return;
    }

    // Send event through keepalive connection
    ret = send(s_keepalive_env.sock, pack_ptr, pack_len, 0);
    if (ret < 0) {
        LOGE("%s: Failed to send event, opcode=%u, err=%d\n", __func__, opcode, errno);
    } else {
        LOGD("%s: Event sent successfully, opcode=%u, packed_len=%u\n", __func__, opcode, pack_len);
    }
}

static int db_keepalive_set_socket_timeout(int sock, int timeout_ms)
{
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        LOGE("%s: Failed to set SO_RCVTIMEO: %d\n", __func__, errno);
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        LOGE("%s: Failed to set SO_SNDTIMEO: %d\n", __func__, errno);
        return -1;
    }

    return 0;
}

void db_tx_cmd_recive_callback(uint8_t *data, uint16_t length)
{
    db_cmd_head_t cmd, *ptr = (db_cmd_head_t *)data;

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
        case DBCMD_WAKE_UP_REQUEST:
        {
            LOGI("%s: WAKE_UP_REQUEST\n", __func__);
            //GPIO_UP(50);
           // GPIO_DOWN(50);
            pl_wakeup_host(POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG);
        }
        break;
        default:
        {
            LOGW("%s: Unknown opcode=%u\n", __func__, cmd.opcode);
            doorbell_transmission_event_report(cmd.opcode, EVT_STATUS_UNKNOWN, EVT_FLAGS_COMPLETE);
        }
        break;
    } 
}

static bk_err_t db_keepalive_init_connection(void)
{
    struct sockaddr_in addr;
    uint8_t retry_cnt = 0;
    int ret;

    while (retry_cnt < DB_KEEPALIVE_MAX_RETRY_CNT) {
        retry_cnt++;
        s_keepalive_env.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (s_keepalive_env.sock < 0) {
            LOGE("%s: Create socket failed, err=%d\n", __func__, errno);
            rtos_delay_milliseconds(1000);
            continue;
        }

        db_keepalive_set_socket_timeout(s_keepalive_env.sock, DB_KEEPALIVE_SOCKET_TIMEOUT_MS);

        addr.sin_family = AF_INET;
        addr.sin_port = htons(s_keepalive_env.port);
        addr.sin_addr.s_addr = inet_addr(s_keepalive_env.server);

        ret = connect(s_keepalive_env.sock, (const struct sockaddr *)&addr, sizeof(addr));
        if (ret == -1) {
            LOGE("%s: Connect failed, err=%d\n", __func__, errno);
            closesocket(s_keepalive_env.sock);
            s_keepalive_env.sock = -1;
            rtos_delay_milliseconds(1000);
            continue;
        }

        LOGI("%s: Connected to server[%s] port[%d] successfully\n", 
             __func__, s_keepalive_env.server, s_keepalive_env.port);
        return BK_OK;
    }

    LOGE("%s: Init connection failed after %d retries\n", __func__, DB_KEEPALIVE_MAX_RETRY_CNT);
    s_keepalive_env.sock = -1;
    return BK_FAIL;
}

static void db_keepalive_rtc_timer_handler(aon_rtc_id_t id, uint8_t *name_p, void *param)
{
    if (!s_keepalive_env.keepalive_ongoing) {
        return;
    }
    rtos_set_semaphore(&s_keepalive_env.sema);
}

static void db_keepalive_set_keepalive_rtc(void)
{
    alarm_info_t keepalive_rtc = {
        "da_rtc",
        s_keepalive_env.interval_ms * AON_RTC_MS_TICK_CNT,
        1,
        db_keepalive_rtc_timer_handler,
        NULL
    };
    
    if (s_keepalive_env.interval_ms < DB_KEEPALIVE_RTC_TIMER_THRESHOLD_MS) {
        LOGE("%s: timeout %d invalid! must >= %d ms\n", 
             __func__, s_keepalive_env.interval_ms, DB_KEEPALIVE_RTC_TIMER_THRESHOLD_MS);
        return;
    }
    
    // Save alarm info to env for later use (for stop_keepalive_rtc)
    os_memcpy(&s_keepalive_env.keepalive_rtc, &keepalive_rtc, sizeof(alarm_info_t));
    
    // Force unregister previous if doesn't finish
    bk_alarm_unregister(AON_RTC_ID_1, keepalive_rtc.name);
    bk_alarm_register(AON_RTC_ID_1, &keepalive_rtc);
    bk_pm_wakeup_source_set(PM_WAKEUP_SOURCE_INT_RTC, NULL);
    bk_pm_sleep_mode_set(PM_MODE_LOW_VOLTAGE);
    LOGD("%s: Set keepalive RTC, interval=%u ms\n", __func__, s_keepalive_env.interval_ms);
}

static void db_keepalive_stop_keepalive_rtc(void)
{
    // Force unregister previous if doesn't finish
    bk_alarm_unregister(AON_RTC_ID_1, s_keepalive_env.keepalive_rtc.name);
}

static void db_keepalive_timer_handler(void *data)
{
    LOGI("%s %d\n", __func__, __LINE__);
    cif_start_lv_sleep();
    // Set RTC timer for low voltage sleep wakeup
    // Note: This will be called after initial delay, then RTC takes over
    if (s_keepalive_env.interval_ms >= DB_KEEPALIVE_RTC_TIMER_THRESHOLD_MS) {
        db_keepalive_set_keepalive_rtc();
    }
}

static void db_keepalive_start_lv_sleep(void)
{
    LOGI("%s %d\n", __func__, __LINE__);
    if (s_keepalive_env.lp_state == PM_MODE_LOW_VOLTAGE) {
        LOGI("%s: Already in LVSLEEP state\n", __func__);
        return;
    }
    s_keepalive_env.lp_state = PM_MODE_LOW_VOLTAGE;
    pl_start_lv_sleep();
}

static void db_keepalive_exit_lv_sleep(void)
{
    LOGI("%s %d\n", __func__, __LINE__);
    s_keepalive_env.lp_state = PM_MODE_NORMAL_SLEEP;
    pl_exit_lv_sleep();
}

static void db_keepalive_send_heartbeat(void)
{
    db_evt_head_t evt;
    uint8_t *pack_ptr;
    uint32_t pack_len;
    int ret;

    if (s_keepalive_env.sock < 0) {
        LOGE("%s: Invalid socket\n", __func__);
        return;
    }

    // Prepare keepalive request event (using db_evt_head_t structure)
    evt.opcode = CHECK_ENDIAN_UINT16(DBCMD_KEEP_ALIVE_REQUEST);
    evt.status = 0;
    evt.flags = 0;
    evt.length = CHECK_ENDIAN_UINT16(0);

    // Pack the event with db_pack header
    ret = db_pack_pack((uint8_t *)&evt, sizeof(db_evt_head_t), &pack_ptr, &pack_len);
    if (ret < 0) {
        LOGE("%s: Failed to pack keepalive request\n", __func__);
        return;
    }

    // Print packed data content for debugging
    //db_keepalive_print_packed_data(pack_ptr, pack_len);

    ret = send(s_keepalive_env.sock, pack_ptr, pack_len, 0);
    if (ret < 0) {
        LOGE("%s: Failed to send keepalive request, err=%d\n", __func__, errno);
    } else {
        LOGD("%s: Keepalive request sent successfully, packed_len=%u\n", __func__, pack_len);
    }
}

static int db_keepalive_recv_raw_data(int sock, uint8_t *rx_raw_buffer, uint16_t max_size, uint16_t *received_len)
{
    int rx_size = 0;
    uint8_t rx_retry_cnt = 0;

    // Receive data from socket, always from buffer start (no accumulation)
rx_retry:
    rx_size = recv(sock, rx_raw_buffer, max_size, 0);
    if (rx_size <= 0) {
        if (rx_size == 0) {
            LOGI("%s: Connection closed by server\n", __func__);
            return -1;
        }
        
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            rx_retry_cnt++;
            if (rx_retry_cnt > DB_KEEPALIVE_RX_MAX_RETRY_CNT) {
                LOGE("%s: RX reaches MAX retry, err=%d\n", __func__, errno);
                return -1;
            }
            goto rx_retry;
        } else {
            LOGE("%s: RX failed, size=%d, err=%d\n", __func__, rx_size, errno);
            return -1;
        }
    }

    *received_len = rx_size;
    return 0; // Success
}

// Callback for db_pack_unpack when a complete packet is received
static void db_keepalive_unpack_callback(uint8_t *data, uint32_t length)
{
    db_cmd_head_t *cmd;

    if (length < sizeof(db_cmd_head_t)) {
        LOGE("%s: too short: %u\n", __func__, length);
        return;
    }

    cmd = (db_cmd_head_t *)data;
    cmd->opcode = CHECK_ENDIAN_UINT32(cmd->opcode);
    cmd->param = CHECK_ENDIAN_UINT32(cmd->param);
    cmd->length = CHECK_ENDIAN_UINT16(cmd->length);

    LOGD("%s: opcode=%u, param=%u, length=%u\n", __func__, cmd->opcode, cmd->param, cmd->length);

    // Handle keepalive response
    if (cmd->opcode == DBCMD_KEEP_ALIVE_RESPONSE) {
        LOGI("%s: Keepalive response successful\n", __func__);
        // Keepalive successful, continue normal operation
    } else {
        // Process other commands (same callback as AP side)
        db_tx_cmd_recive_callback(data, length);
    }
}

static void db_keepalive_rx_handler(void *arg)
{
    int ret;
    uint16_t received_len;

    LOGI("%s: RX handler started\n", __func__);

    while (s_keepalive_env.keepalive_ongoing && s_keepalive_env.sock >= 0) {
        // Receive raw data (with db_pack header)
        ret = db_keepalive_recv_raw_data(s_keepalive_env.sock, 
                                          s_keepalive_env.rx_raw_buffer, 
                                          DB_KEEPALIVE_MSG_BUFFER_SIZE,
                                          &received_len);
        if (ret < 0) {
            LOGE("%s: Receive raw data failed\n", __func__);
            goto _exit;
        }

        if (received_len == 0) {
            continue;
        }

        // Unpack the received data using db_pack
        ret = db_pack_unpack(s_keepalive_env.rx_raw_buffer, received_len, 
                            db_keepalive_unpack_callback);
        if (ret < 0) {
            LOGW("%s: Unpack failed, ret=%d\n", __func__, ret);
            // Continue to receive next packet
        }
    }

_exit:
    LOGE("%s: RX handler exited\n", __func__);
    db_keepalive_cp_deinit();
}

static bk_err_t db_keepalive_init_rx(void)
{
    bk_err_t ret;

    ret = rtos_create_thread(&s_keepalive_env.rx_thread,
                              DB_KEEPALIVE_TASK_PRIO,
                              "db_ka_rx",
                              (beken_thread_function_t)db_keepalive_rx_handler,
                              2048,
                              NULL);
    if (ret != BK_OK) {
        LOGE("%s: Failed to create RX thread\n", __func__);
        db_keepalive_cp_deinit();
    }

    return ret;
}

static void db_keepalive_tx_handler(void *arg)
{
    int ret;

    LOGI("%s: TX handler started\n", __func__);

    s_host_powered_down = false;
    s_notify_ap_keepalive_failed = false;

    // Add CIF filter for keepalive server
    cif_filter_add_customer_filter(inet_addr(s_keepalive_env.server), s_keepalive_env.port);

    // Initialize connection
    if (db_keepalive_init_connection() != BK_OK) {
        LOGE("%s: Failed to initialize connection\n", __func__);
        s_notify_ap_keepalive_failed = true;
        goto _exit;
    }

    // Start RX thread
    ret = db_keepalive_init_rx();
    if (ret != BK_OK) {
        LOGE("%s: Failed to init RX\n", __func__);
        s_notify_ap_keepalive_failed = true;
        goto _exit;
    }

    // Power down host
    pl_power_down_host();
    s_host_powered_down = true;

    // Start low voltage sleep
    db_keepalive_start_lv_sleep();

    // Start keepalive timer (initial delay before entering low voltage sleep)
    // After initial delay, RTC timer will take over for low voltage sleep wakeup
    ret = rtos_init_timer(&s_keepalive_env.timer,
                          s_keepalive_env.interval_ms,
                          db_keepalive_timer_handler,
                          NULL);
    if (ret != BK_OK) {
        LOGE("%s: Failed to init timer\n", __func__);
        s_notify_ap_keepalive_failed = true;
        goto _exit;
    }

    ret = rtos_start_timer(&s_keepalive_env.timer);
    if (ret != BK_OK) {
        LOGE("%s: Failed to start timer\n", __func__);
        s_notify_ap_keepalive_failed = true;
        goto _exit;
    }

    LOGI("%s: Keepalive started, interval=%u ms\n", __func__, s_keepalive_env.interval_ms);

    // Send first keepalive immediately after connection is established
    if (s_keepalive_env.keepalive_ongoing && s_keepalive_env.sock >= 0) {
        LOGI("%s: Sending first keepalive immediately\n", __func__);
        db_keepalive_send_heartbeat();
    }

    // Main loop: send heartbeat periodically
    // Use RTC timer for low voltage sleep wakeup
    while (s_keepalive_env.keepalive_ongoing) {
        // Set RTC timer for next wakeup
        db_keepalive_set_keepalive_rtc();
        
        ret = rtos_get_semaphore(&s_keepalive_env.sema, BEKEN_WAIT_FOREVER);
        if (ret != BK_OK) {
            LOGW("%s: Get semaphore failed\n", __func__);
            break;
        }

        if (s_keepalive_env.keepalive_ongoing && s_keepalive_env.sock >= 0) {
            db_keepalive_send_heartbeat();
        }
    }

_exit:
    if (s_notify_ap_keepalive_failed) {
        if (s_host_powered_down) {
            pl_wakeup_host(POWERUP_KEEPALIVE_FAIL_WAKEUP_FLAG);
        } else {
            pl_set_wakeup_reason(POWERUP_KEEPALIVE_DISCONNECTION);
            db_ipc_send_event(DB_IPC_EVENT_KEEPALIVE_DISCONNECTION, NULL, 0);
        }
    }
    LOGI("%s: TX handler exited\n", __func__);
    db_keepalive_cp_deinit();
}

bk_err_t db_keepalive_cp_init(db_ipc_keepalive_cfg_t *cfg)
{
    bk_err_t ret;

    if (cfg == NULL) {
        LOGE("%s: Invalid config\n", __func__);
        return BK_FAIL;
    }

    if (s_keepalive_env.keepalive_ongoing) {
        LOGW("%s: Keepalive already ongoing\n", __func__);
        return BK_FAIL;
    }

    os_memset(&s_keepalive_env, 0, sizeof(db_keepalive_env_t));

    // Copy configuration
    os_strncpy(s_keepalive_env.server, cfg->server, sizeof(s_keepalive_env.server) - 1);
    s_keepalive_env.server[sizeof(s_keepalive_env.server) - 1] = '\0';
    s_keepalive_env.port = cfg->port;
    s_keepalive_env.interval_ms = DB_KEEPALIVE_DEFAULT_INTERVAL_MS;

    // Allocate RX buffer for unpacked data
    s_keepalive_env.rx_buffer = (uint8_t *)os_malloc(DB_KEEPALIVE_MSG_BUFFER_SIZE);
    if (s_keepalive_env.rx_buffer == NULL) {
        LOGE("%s: Failed to allocate RX buffer\n", __func__);
        return BK_FAIL;
    }

    // Allocate RX raw buffer for packed data (with db_pack header)
    s_keepalive_env.rx_raw_buffer = (uint8_t *)os_malloc(DB_KEEPALIVE_MSG_BUFFER_SIZE);
    if (s_keepalive_env.rx_raw_buffer == NULL) {
        LOGE("%s: Failed to allocate RX raw buffer\n", __func__);
        os_free(s_keepalive_env.rx_buffer);
        s_keepalive_env.rx_buffer = NULL;
        return BK_FAIL;
    }

    // Initialize db_pack module
    // max_rx_size and max_tx_size should accommodate db_cmd_head_t + payload
    ret = db_pack_init(DB_KEEPALIVE_MSG_BUFFER_SIZE, DB_KEEPALIVE_MSG_BUFFER_SIZE);
    if (ret != BK_OK) {
        LOGE("%s: Failed to init db_pack\n", __func__);
        os_free(s_keepalive_env.rx_raw_buffer);
        s_keepalive_env.rx_raw_buffer = NULL;
        os_free(s_keepalive_env.rx_buffer);
        s_keepalive_env.rx_buffer = NULL;
        return BK_FAIL;
    }

    // Initialize semaphore
    if (rtos_init_semaphore(&s_keepalive_env.sema, 1) != BK_OK) {
        LOGE("%s: Failed to init semaphore\n", __func__);
        db_pack_deinit();
        os_free(s_keepalive_env.rx_raw_buffer);
        s_keepalive_env.rx_raw_buffer = NULL;
        os_free(s_keepalive_env.rx_buffer);
        s_keepalive_env.rx_buffer = NULL;
        return BK_FAIL;
    }

    s_keepalive_env.keepalive_ongoing = true;
    s_keepalive_env.sock = -1;
    s_keepalive_env.lp_state = PM_MODE_NORMAL_SLEEP;

    LOGI("%s: Initialized, server=%s, port=%d\n", __func__, s_keepalive_env.server, s_keepalive_env.port);

    return BK_OK;
}

bk_err_t db_keepalive_cp_start(void)
{
    int ret;

    if (!s_keepalive_env.keepalive_ongoing) {
        LOGE("%s: Not initialized\n", __func__);
        return BK_FAIL;
    }

    // Create TX thread
    ret = rtos_create_thread(&s_keepalive_env.tx_thread,
                              DB_KEEPALIVE_TASK_PRIO,
                              "db_ka_tx",
                              (beken_thread_function_t)db_keepalive_tx_handler,
                              2048,
                              NULL);
    if (ret != BK_OK) {
        LOGE("%s: Failed to create TX thread\n", __func__);
        return BK_FAIL;
    }

    LOGI("%s: Started\n", __func__);
    return BK_OK;
}

bk_err_t db_keepalive_cp_stop(void)
{
    s_keepalive_env.keepalive_ongoing = false;
    return db_keepalive_cp_deinit();
}

bk_err_t db_keepalive_cp_deinit(void)
{
    LOGI("%s: Deinitializing\n", __func__);

    s_keepalive_env.keepalive_ongoing = false;

    // Remove CIF filter
    cif_filter_add_customer_filter(0, 0);

    // Stop RTC timer
    db_keepalive_stop_keepalive_rtc();

    // Exit low voltage sleep if in that state
    if (s_keepalive_env.lp_state == PM_MODE_LOW_VOLTAGE) {
        db_keepalive_exit_lv_sleep();
    }

    // Stop timer
    if (s_keepalive_env.timer.handle) {
        rtos_stop_timer(&s_keepalive_env.timer);
        rtos_deinit_timer(&s_keepalive_env.timer);
        s_keepalive_env.timer.handle = NULL;
    }

    // Close socket
    if (s_keepalive_env.sock >= 0) {
        closesocket(s_keepalive_env.sock);
        s_keepalive_env.sock = -1;
    }

    // Delete threads
    if (s_keepalive_env.tx_thread) {
        rtos_delete_thread(&s_keepalive_env.tx_thread);
        s_keepalive_env.tx_thread = NULL;
    }

    if (s_keepalive_env.rx_thread) {
        rtos_delete_thread(&s_keepalive_env.rx_thread);
        s_keepalive_env.rx_thread = NULL;
    }

    // Deinitialize db_pack module
    db_pack_deinit();

    // Free resources
    if (s_keepalive_env.rx_buffer) {
        os_free(s_keepalive_env.rx_buffer);
        s_keepalive_env.rx_buffer = NULL;
    }

    if (s_keepalive_env.rx_raw_buffer) {
        os_free(s_keepalive_env.rx_raw_buffer);
        s_keepalive_env.rx_raw_buffer = NULL;
    }

    if (s_keepalive_env.sema) {
        rtos_deinit_semaphore(&s_keepalive_env.sema);
        s_keepalive_env.sema = NULL;
    }

    s_keepalive_env.lp_state = PM_MODE_NORMAL_SLEEP;
    os_memset(&s_keepalive_env, 0, sizeof(db_keepalive_env_t));

    LOGI("%s: Deinitialized\n", __func__);
    return BK_OK;
}
