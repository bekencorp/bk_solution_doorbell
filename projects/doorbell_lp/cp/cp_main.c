#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include <modules/pm.h>
#include <driver/pwr_clk.h>
#include "doorbell_comm.h"
#include <components/ate.h>
#include "powerctrl.h"
#include "db_ipc_msg.h"

extern void rtos_set_user_app_entry(beken_thread_function_t entry);
extern void bk_set_jtag_mode(uint32_t cpu_id, uint32_t group_id);

void user_app_main(void) {
    pl_wakeup_host(POWERUP_POWER_WAKEUP_FLAG);


    if (!ate_is_enabled())
    {
        db_ipc_msg_init();
#if !CONFIG_BTDM_CONTROLLER_ONLY
        doorbell_core_init();
#endif
    }


}

int main(void)
{
    rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
    bk_init();

    return 0;
}