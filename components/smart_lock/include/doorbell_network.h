#ifndef __DOORBELL_NETWORK_H__
#define __DOORBELL_NETWORK_H__

#include "lwip/sockets.h"
#include "net.h"
#include "doorbell_boarding.h"
#include "network_transfer.h"

typedef struct db_wifi_connect_info
{
    uint8_t db_ssid[33];
    uint8_t db_pwd[65];
}db_wifi_connect_info_t;

typedef struct db_ntwk_service_info
{
    uint8_t db_service;
}db_ntwk_service_info_t;

int doorbell_wifi_sta_connect(char *ssid, char *key);
int doorbell_wifi_soft_ap_start(char *ssid, char *key, uint16_t channel);

bk_err_t doorbell_save_wifi_info_to_flash(doorbell_boarding_info_t *boarding_info);
bk_err_t doorbell_get_wifi_info_from_flash(db_wifi_connect_info_t *wifi_info);
bk_err_t doorbell_save_ntwk_service_info_to_flash(db_ntwk_service_info_t *service_info);
bk_err_t doorbell_get_ntwk_service_info_from_flash(db_ntwk_service_info_t *service_info);
bk_err_t doorbell_save_keepalive_interval_to_flash(uint32_t interval_ms);
bk_err_t doorbell_get_keepalive_interval_from_flash(uint32_t *interval_ms);
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
bk_err_t doorbell_save_server_net_info_to_flash(uint8_t *data);
bk_err_t doorbell_get_server_net_info_from_flash(ntwk_server_net_info_t *net_info);
#endif

#endif
