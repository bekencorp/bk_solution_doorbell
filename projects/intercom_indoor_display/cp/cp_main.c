#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include <modules/pm.h>
#include <driver/pwr_clk.h>
#include "bk_api_ipc_test.h"
#include <components/ate.h>
#include "powerctrl.h"
#include "db_ipc_msg.h"

extern void rtos_set_user_app_entry(beken_thread_function_t entry);


void user_app_main(void) {
    if (!ate_is_enabled())
    {
        db_ipc_msg_init();
    }

    pl_wakeup_host(POWERUP_POWER_WAKEUP_FLAG);
}

int main(void)
{
    rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
    bk_init();

    return 0;
}