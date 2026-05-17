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

void doorbell_transmission_cmd_recive_callback(uint8_t *data, uint16_t length);

#endif
