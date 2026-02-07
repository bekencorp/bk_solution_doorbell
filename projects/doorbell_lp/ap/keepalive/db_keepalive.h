#pragma once


#ifdef __cplusplus
extern "C" {
#endif

#define MM_STATUS_CHECK_INTERVAL_MS (5 * 1000)
#define MM_STATUS_CHECK_MIN_INTERVAL_MS (1 * 1000)


typedef enum
{
    POWERUP_POWER_WAKEUP_FLAG = 1,                   //Normal power-on startup
    POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG = 2,         //Multimedia Wake up request
    POWERUP_KEEPALIVE_DISCONNECTION = 3,             //Keepalive disconnection startup
    POWERUP_KEEPALIVE_FAIL_WAKEUP_FLAG = 4,          //Keepalive failure wakeup
}POWERUP_FLAGS;


void db_keepalive_handle_wakeup_reason(void);
void db_keepalive_on_service_start_success(void);
void db_keepalive_on_keepalive_disconnection(void);
bk_err_t db_keepalive_start_mm_status_check(void);
bk_err_t db_keepalive_stop_mm_status_check(void);
void db_keepalive_update_timestamp(void);
void db_keepalive_send_keepalive(void);
int db_keepalive_cli_init(void);

#ifdef __cplusplus
}
#endif


