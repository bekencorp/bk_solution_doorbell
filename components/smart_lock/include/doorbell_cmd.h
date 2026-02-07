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

// Command opcode enumeration
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

void doorbell_transmission_cmd_recive_callback(uint8_t *data, uint16_t length);

/**
 * @brief Multimedia service status bit flags
 * Each bit represents a multimedia service status vote
 */
typedef enum
{
    MM_STATUS_CAMERA_BIT = 0,    // Bit 0: Camera service status
    MM_STATUS_AUDIO_BIT = 1,     // Bit 1: Audio service status
    MM_STATUS_LCD_BIT = 2,       // Bit 2: LCD service status
} mm_status_bit_t;

#define MM_STATUS_CAMERA_MASK (1U << MM_STATUS_CAMERA_BIT)
#define MM_STATUS_AUDIO_MASK  (1U << MM_STATUS_AUDIO_BIT)
#define MM_STATUS_LCD_MASK    (1U << MM_STATUS_LCD_BIT)

/**
 * @brief Vote for multimedia service status
 * 
 * This function implements a voting mechanism where multiple modules can vote
 * for a multimedia service to be on or off. Each vote sets a bit in the status bitmap.
 * The service will only be turned off when all votes are removed (bit is cleared).
 * 
 * @param[in]  service_bit  Service bit flag (MM_STATUS_CAMERA_BIT, MM_STATUS_AUDIO_BIT, MM_STATUS_LCD_BIT)
 * @param[in]  vote_add     If true, add a vote (set bit), if false, remove a vote (clear bit)
 * 
 * @return     Current status bitmap after voting
 */
uint32_t doorbell_mm_service_vote(mm_status_bit_t service_bit, bool vote_add);

/**
 * @brief Get multimedia service status information
 * 
 * This function returns the current status of all multimedia services.
 * The status is encoded as bit flags where each bit indicates if a service
 * has active votes.
 * 
 * @return  Status bitmap:
 *          - Bit 0: Camera service status (1 = has votes, 0 = no votes)
 *          - Bit 1: Audio service status (1 = has votes, 0 = no votes)
 *          - Bit 2: LCD service status (1 = has votes, 0 = no votes)
 */
uint32_t doorbell_mm_service_get_status(void);

#endif
