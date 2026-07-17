#ifndef __DOORBELL_NETWORK_TRANSFER_H__
#define __DOORBELL_NETWORK_TRANSFER_H__

typedef enum
{
    DB_NTWK_TYPE_NONE = 0,
    DB_NTWK_TYPE_TCP = 1,
    DB_NTWK_TYPE_UDP = 2,
    DB_NTWK_TYPE_CS2 = 3,
} db_ntwk_type_t;

bk_err_t doorbell_bk_network_transfer_init(char *service_name, void *param);
bk_err_t doorbell_bk_network_transfer_deinit(char *service_name);

#endif
