#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <media_service.h>
#include "bk_smart_config.h"
#include <doorbell_comm.h>


int main(void)
{
	bk_init();
    media_service_init();

#if (defined(CONFIG_INTEGRATION_DOORBELL) || defined(CONFIG_INTEGRATION_DOORVIEWER))
    bk_smart_config_init();
    doorbell_core_init();
#endif
	return 0;
}
