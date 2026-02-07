#pragma once

#include <common/bk_include.h>
#include <os/os.h>
#include <components/log.h>
#include "db_ipc_msg.h"
#include <lwip/sockets.h>
#include <driver/aon_rtc_types.h>
#include <driver/aon_rtc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DB_KEEPALIVE_TASK_PRIO       4

#define DB_KEEPALIVE_RX_MAX_RETRY_CNT 20

// Default keepalive interval in milliseconds
#define DB_KEEPALIVE_DEFAULT_INTERVAL_MS (30 * 1000)  // 30 seconds

// Socket timeout settings
#define DB_KEEPALIVE_SOCKET_TIMEOUT_MS (3000)  // 3 seconds
#define DB_KEEPALIVE_MAX_RETRY_CNT (5)

// Message buffer size
#define DB_KEEPALIVE_MSG_BUFFER_SIZE (1460)

// RTC timer threshold (minimum interval for RTC timer)
#define DB_KEEPALIVE_RTC_TIMER_THRESHOLD_MS (500)


#define TRANSMISSION_BIG_ENDIAN (BK_FALSE)

#if TRANSMISSION_BIG_ENDIAN == BK_TRUE
#define CHECK_ENDIAN_UINT32(var)    htonl(var)
#define CHECK_ENDIAN_UINT16(var)    htons(var)
#else
#define CHECK_ENDIAN_UINT16
#define CHECK_ENDIAN_UINT32
#endif

#define EVT_STATUS_OK               (0)
#define EVT_STATUS_ERROR            (1)
#define EVT_STATUS_UNKNOWN          (12)
#define EVT_FLAGS_COMPLETE          (0 << 0)

typedef struct
{
    uint32_t  opcode;
    uint32_t  param;
    uint16_t  length;
    uint8_t  payload[];
} __attribute__((__packed__)) db_cmd_head_t;

typedef struct
{
    uint32_t  opcode;
    uint8_t  status;
    uint16_t  flags;
    uint16_t  length;
    uint8_t  payload[];
} __attribute__((__packed__)) db_evt_head_t;

typedef struct {
    bool keepalive_ongoing;
    int sock;
    char server[32];
    uint16_t port;
    uint32_t interval_ms;
    beken_timer_t timer;
    beken_thread_t tx_thread;
    beken_thread_t rx_thread;
    beken_semaphore_t sema;
    uint8_t *rx_buffer;        // Buffer for unpacked data (db_cmd_head_t)
    uint8_t *rx_raw_buffer;     // Buffer for raw received data (with db_pack header)
    uint8_t lp_state;  // Low power state: PM_MODE_LOW_VOLTAGE or PM_MODE_NORMAL_SLEEP
    alarm_info_t keepalive_rtc;  // RTC alarm for low voltage sleep wakeup
} db_keepalive_env_t;

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

    DBCMD_KEEP_ALIVE_REQUEST = 17,
    DBCMD_KEEP_ALIVE_RESPONSE = 18,
    DBCMD_WAKE_UP_REQUEST = 19,

    DBCMD_PNG = 100,

} dbcmd_t;

/**
 * @brief Initialize keepalive module
 * 
 * @param cfg Keepalive configuration from AP side
 * @return bk_err_t BK_OK on success, BK_FAIL on error
 */
bk_err_t db_keepalive_cp_init(db_ipc_keepalive_cfg_t *cfg);

/**
 * @brief Deinitialize keepalive module
 * 
 * @return bk_err_t BK_OK on success, BK_FAIL on error
 */
bk_err_t db_keepalive_cp_deinit(void);

/**
 * @brief Start keepalive
 * 
 * @return bk_err_t BK_OK on success, BK_FAIL on error
 */
bk_err_t db_keepalive_cp_start(void);

/**
 * @brief Stop keepalive
 * 
 * @return bk_err_t BK_OK on success, BK_FAIL on error
 */
bk_err_t db_keepalive_cp_stop(void);

#ifdef __cplusplus
}
#endif
