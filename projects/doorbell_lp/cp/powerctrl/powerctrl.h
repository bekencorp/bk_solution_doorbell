#pragma once

#include <components/log.h>
#include <modules/wifi.h>
#include <components/netif.h>
#include <components/event.h>
#include <string.h>
#include "bk_private/bk_init.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PL_DISABLE_FACTORYMODE                   1
#define PL_DEBUG_CODE_MAGIC                      (0xAABBCCDD)
#define MAX_KEY_STR_SIZE                           64


typedef enum
{
    POWERUP_POWER_WAKEUP_FLAG = 1,                   //Normal power-on startup
    POWERUP_MULTIMEDIA_WAKEUP_HOST_FLAG = 2,         //Multimedia Wake up request
    POWERUP_KEEPALIVE_DISCONNECTION = 3,             //Keepalive disconnection startup
    POWERUP_KEEPALIVE_FAIL_WAKEUP_FLAG = 4,          //Keepalive failure wakeup
}POWERUP_FLAGS;

typedef struct
{
    uint32_t wakeup_reason;
    beken2_timer_t timer;
    uint8_t delay_action;
    uint32_t delay_arg1;
    uint32_t delay_arg2;
}pl_wakeup_t;


extern pl_wakeup_t pl_wakeup_env;

static __inline__ uint32_t pl_get_wakeup_reason()
{
    os_printf("get wakeup_reason:%x\n", pl_wakeup_env.wakeup_reason);
    return pl_wakeup_env.wakeup_reason;
}
static __inline__ void pl_reset_wakeup_reason(void)
{
    pl_wakeup_env.wakeup_reason = 0;
}
static __inline__ void pl_set_wakeup_reason(POWERUP_FLAGS reason)
{
    os_printf("set wakeup_reason:%x\n", reason);
    pl_wakeup_env.wakeup_reason |= reason;
}
static __inline__ void pl_clear_wakeup_reason(POWERUP_FLAGS reason)
{
    pl_wakeup_env.wakeup_reason &= ~reason;
}

void pl_wakeup_host(uint32_t flag);
void pl_power_down_host(void);
void pl_start_lv_sleep(void);
void pl_exit_lv_sleep(void);

#ifdef __cplusplus
}
#endif
