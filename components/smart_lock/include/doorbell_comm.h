#ifndef __DOORBELL_COMM_H__
#define __DOORBELL_COMM_H__

#if (CONFIG_EASY_FLASH && CONFIG_EASY_FLASH_V4)
#include "bk_ef.h"
#endif

#define UVC_DEVICE_ID (0xFDF6)

typedef enum
{
    DBEVT_WIFI_STATION_CONNECT,
    DBEVT_WIFI_STATION_CONNECTED,
    DBEVT_WIFI_STATION_DISCONNECTED,

    DBEVT_P2P_CS2_SERVICE_START_REQUEST,
    DBEVT_P2P_CS2_SERVICE_START_RESPONSE,

    DBEVT_LAN_UDP_SERVICE_START_REQUEST,
    DBEVT_LAN_UDP_SERVICE_START_RESPONSE,

    DBEVT_LAN_TCP_SERVICE_START_REQUEST,
    DBEVT_LAN_TCP_SERVICE_START_RESPONSE,


    DBEVT_WIFI_SOFT_AP_TURNING_ON,

    DBEVT_REMOTE_DEVICE_CONNECTED,
    DBEVT_REMOTE_DEVICE_DISCONNECTED,

    DBEVT_START_WIFI_STATION,
    DBEVT_START_TCP_SERVICE,
    DBEVT_START_BOARDING_EVENT,
    DBEVT_BLE_DISABLE,
    DBEVT_SDP,
    DBEVT_EXIT,

    DBEVT_IMAGE_TCP_SERVICE_DISCONNECTED,
    DBEVT_VOICE_EVENT,

    DBEVT_SET_SERVER_NET_INFO,
    DBEVT_LAN_TCP_SERVICE_STOP,
    DBEVT_LAN_UDP_SERVICE_STOP,
} dbevt_t;

typedef enum
{
    DOORBELL_SERVICE_NONE = 0,
    DOORBELL_SERVICE_P2P_CS2 = 1,
    DOORBELL_SERVICE_LAN_UDP = 2,
    DOORBELL_SERVICE_LAN_TCP = 3
} doorbell_service_t;


typedef struct
{
    uint32_t event;
    uint32_t param;
} doorbell_msg_t;

typedef enum
{
    DB_TURN_OFF,
    DB_TURN_ON,
} doorbell_state_t;

bk_err_t doorbell_send_msg(doorbell_msg_t *msg);

void doorbell_core_init(void);


typedef struct
{
    int (*init)(void *param);
    void (*deinit)(void);
    int (*camera_state_changed)(doorbell_state_t state);
    int (*audio_state_changed)(doorbell_state_t state);
    const void *camera_transfer_cb;
    const void *audio_transfer_cb;
} doorbell_service_interface_t;



#endif
