#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include "bk_api_ipc_test.h"

extern void rtos_set_user_app_entry(beken_thread_function_t entry);

static void user_app_main(void)
{
    bk_start_ap_system();
}

int main(void)
{
    rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
    bk_init();

#if (BK_IPC_UT_TEST)
    bk_ipc_test_init();
#endif

    return 0;
}
