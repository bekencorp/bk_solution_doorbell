#ifndef __DOORBELL_CMD_H__
#define __DOORBELL_CMD_H__

#include "lwip/inet.h"
#include "network_transfer.h"

#define EVT_STATUS_OK               (0)
#define EVT_STATUS_ERROR            (1)
#define EVT_STATUS_ALREADY          (2)
#define EVT_STATUS_NULL             (11)
#define EVT_STATUS_UNKNOWN          (12)


#define EVT_FLAGS_COMPLETE          (0 << 0)
#define EVT_FLAGS_CONTINUE          (1 << 0)


#define OPCODE_NOTIFICATION         (1 << 31)


/*
*   cmd
*/

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

#define DEVICE_RESPONSE_SIZE (NTWK_TRANS_DATA_MAX_SIZE - sizeof(db_evt_head_t))

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
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    DBCMD_KEEP_ALIVE_REQUEST = 17,
    DBCMD_KEEP_ALIVE_RESPONSE = 18,
    DBCMD_WAKE_UP_REQUEST = 19,
    DBCMD_DEVICE_POWER_ON_NOTIFY = 20,
#endif
    DBCMD_PNG = 100,
} dbcmd_t;

void doorbell_transmission_cmd_recive_callback(uint8_t *data, uint16_t length);

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
void doorbell_transmission_device_power_on_notify(void);

typedef enum
{
    MM_STATUS_CAMERA_BIT = 0,
    MM_STATUS_AUDIO_BIT = 1,
    MM_STATUS_LCD_BIT = 2,
    MM_STATUS_ALL_BIT = 3,
} mm_status_bit_t;

#define MM_STATUS_CAMERA_MASK (1U << MM_STATUS_CAMERA_BIT)
#define MM_STATUS_AUDIO_MASK  (1U << MM_STATUS_AUDIO_BIT)
#define MM_STATUS_LCD_MASK    (1U << MM_STATUS_LCD_BIT)
#define MM_STATUS_ALL_MASK (MM_STATUS_CAMERA_MASK | MM_STATUS_AUDIO_MASK | MM_STATUS_LCD_MASK)

uint32_t doorbell_mm_service_vote(mm_status_bit_t service_bit, bool vote_add);
uint32_t doorbell_mm_service_get_status(void);

/*
 * UI-mode hook driven by the multimedia service status edges.
 *
 * A local-UI product (e.g. an LVGL home screen) must yield the LCD to the
 * two-way video-call pipeline while a call is up and take it back when every
 * feature is off. doorbell_mm_service_vote() aggregates the camera / audio / lcd
 * votes, so it is the single choke point that knows when a call starts and ends:
 *   - active = true  on the idle -> active edge (status goes 0 -> non-zero)
 *   - active = false on the active -> idle edge (status goes non-zero -> 0)
 *
 * The callback runs on whatever thread called the vote (network / RPC thread);
 * keep it light. Pass cb = NULL to unregister.
 */
typedef void (*doorbell_mm_status_cb_t)(bool active, void *user);
void doorbell_mm_service_set_status_cb(doorbell_mm_status_cb_t cb, void *user);
#endif
#endif
