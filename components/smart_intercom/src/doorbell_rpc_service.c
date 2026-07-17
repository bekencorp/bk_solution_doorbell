#include <common/bk_include.h>
#include <os/str.h>
#include <os/os.h>

#include "cJSON.h"

#include "doorbell_comm.h"
#include "doorbell_rpc_internal.h"

#define TAG "db-rpc-svc"
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* doorbell.service.setType : set data-stream transport service type (udp/tcp). */
bk_err_t doorbell_rpc_service_set_type(cJSON *params, cJSON *id)
{
    cJSON *service_type = params ? cJSON_GetObjectItem(params, "serviceType") : NULL;
    doorbell_msg_t msg = {0};

    if (service_type == NULL || !cJSON_IsString(service_type))
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid serviceType", NULL);
    }

    if (os_strcmp(service_type->valuestring, "udp") == 0)
    {
        msg.event = DBEVT_LAN_UDP_SERVICE_START_REQUEST;
    }
    else if (os_strcmp(service_type->valuestring, "tcp") == 0)
    {
        msg.event = DBEVT_LAN_TCP_SERVICE_START_REQUEST;
    }
    else
    {
        cJSON *data = cJSON_CreateObject();
        cJSON *allowed = cJSON_CreateArray();
        if (data != NULL && allowed != NULL)
        {
            cJSON_AddItemToArray(allowed, cJSON_CreateString("udp"));
            cJSON_AddItemToArray(allowed, cJSON_CreateString("tcp"));
            cJSON_AddItemToObject(data, "allowed", allowed);
        }
        else
        {
            if (allowed != NULL) cJSON_Delete(allowed);
            if (data != NULL) { cJSON_Delete(data); data = NULL; }
        }
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid serviceType", data);
    }

    LOGD("service.setType: %s\n", service_type->valuestring);
    doorbell_send_msg(&msg);

    return doorbell_rpc_send_result_null(id);
}
