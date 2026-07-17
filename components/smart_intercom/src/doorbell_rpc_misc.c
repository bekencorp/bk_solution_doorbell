#include <common/bk_include.h>

#include "cJSON.h"

#include "doorbell_rpc_internal.h"

/* doorbell.misc.ping : liveness probe. */
bk_err_t doorbell_rpc_misc_ping(cJSON *params, cJSON *id)
{
    (void)params;
    return doorbell_rpc_send_result_null(id);
}
