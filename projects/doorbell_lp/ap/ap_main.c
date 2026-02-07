#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <media_service.h>
#include "bk_smart_config.h"
#include <doorbell_comm.h>
#include "db_ipc_msg/db_ipc_msg.h"
#include "keepalive/db_keepalive.h"

int main(void)
{
    bk_init();
    media_service_init();
    #if (defined(CONFIG_INTEGRATION_DOORBELL) || defined(CONFIG_INTEGRATION_DOORVIEWER))
    bk_smart_config_init();
    doorbell_core_init();
    #endif

    db_ipc_wakeup_env_init();
    db_keepalive_handle_wakeup_reason();
    db_keepalive_cli_init();
    return 0;
}
