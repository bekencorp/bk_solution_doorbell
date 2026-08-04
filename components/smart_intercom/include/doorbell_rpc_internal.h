#ifndef __DOORBELL_RPC_INTERNAL_H__
#define __DOORBELL_RPC_INTERNAL_H__

#include <common/bk_err.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* JSON-RPC 2.0 standard error codes */
#define DB_RPC_ERR_PARSE        (-32700) /* JSON parse error            */
#define DB_RPC_ERR_INVALID_REQ  (-32600) /* invalid request            */
#define DB_RPC_ERR_METHOD       (-32601) /* method not found           */
#define DB_RPC_ERR_PARAMS       (-32602) /* invalid params             */
#define DB_RPC_ERR_INTERNAL     (-32603) /* internal error             */
/* doorbell private code: capability/params exceed device support.      */
#define DB_RPC_ERR_NOT_SUPPORT  (-32003)

/* Handler signature: params/id are borrowed from the parsed request root
 * and MUST NOT be deleted by the handler. */
typedef bk_err_t (*db_rpc_handler_t)(cJSON *params, cJSON *id);

typedef struct
{
    const char *method;
    db_rpc_handler_t fn;
} db_rpc_entry_t;

/* ---- response / notification helpers (defined in doorbell_jsonrpc.c) ---- */

/* Send a success response. @p result ownership is transferred (deleted by the
 * helper); pass NULL to return "result": null. @p id is duplicated. */
bk_err_t doorbell_rpc_send_result(cJSON *id, cJSON *result);

/* Convenience: "result": null. */
bk_err_t doorbell_rpc_send_result_null(cJSON *id);

/* Convenience: "result": { "status": <status> }. */
bk_err_t doorbell_rpc_send_status(cJSON *id, const char *status);

/* Send an error response. @p data ownership is transferred (deleted by the
 * helper); pass NULL for no data field. @p id may be NULL (-> null id). */
bk_err_t doorbell_rpc_send_error(cJSON *id, int code, const char *message, cJSON *data);

/* ---- domain handlers ---- */
bk_err_t doorbell_rpc_service_set_type(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_camera_turn_on(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_camera_turn_off(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_camera_get_status(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_audio_turn_on(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_audio_turn_off(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_audio_get_status(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_audio_set_acoustics(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_lcd_turn_on(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_lcd_turn_off(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_lcd_get_status(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_image_set_receive_config(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_misc_ping(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_solution_get_config(cJSON *params, cJSON *id);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_RPC_INTERNAL_H__ */
