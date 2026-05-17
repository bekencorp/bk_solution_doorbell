#ifndef __DOORBELL_NETWORK_H__
#define __DOORBELL_NETWORK_H__

#include "lwip/sockets.h"
#include "net.h"

int doorbell_wifi_sta_connect(char *ssid, char *key);
int doorbell_wifi_soft_ap_start(char *ssid, char *key, uint16_t channel);

#endif
