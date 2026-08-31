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

/* ---- device working mode ----
 *
 * The two capture/display use cases are mutually exclusive and selected by the
 * command the App issues:
 *   - DB_WORK_MODE_SINGLE   : single-direction image transfer, opened by
 *                             doorbell.camera.turnOn (local MIPI capture ->
 *                             uplink to App + local MIPI preview on the panel).
 *   - DB_WORK_MODE_INTERCOM : two-way video intercom, opened by
 *                             doorbell.videoIntercom.turnOn (uplink capture +
 *                             downlink receive/decode/display in one command).
 * While in INTERCOM mode the uplink/downlink links MUST NOT be toggled
 * individually via doorbell.camera.turnOn/turnOff, so those handlers reject the
 * request and defer to doorbell.videoIntercom.turnOff. */
typedef enum
{
    DB_WORK_MODE_IDLE = 0,
    DB_WORK_MODE_SINGLE,
    DB_WORK_MODE_INTERCOM,
} db_work_mode_t;

db_work_mode_t doorbell_rpc_work_mode_get(void);
void doorbell_rpc_work_mode_set(db_work_mode_t mode);

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

/* Shared uplink helpers (defined in doorbell_rpc_camera.c). Used by both
 * doorbell.camera.turnOn (single-direction mode) and
 * doorbell.videoIntercom.turnOn (uplink half of two-way mode) so the capture +
 * H.264 encode + video-transfer bring-up/tear-down lives in one place.
 *
 * doorbell_rpc_camera_uplink_open_from_params() parses a camera.turnOn-style
 * params object ("streamCount" + "streams"[]), opens the uplink with rollback,
 * and on failure fills *err_code (a DB_RPC_ERR_* value) and *err_msg for the
 * caller's RPC error reply. Returns BK_OK on success. */
bk_err_t doorbell_rpc_camera_uplink_open_from_params(cJSON *params, int *err_code, const char **err_msg);
bk_err_t doorbell_rpc_camera_uplink_close(void);

bk_err_t doorbell_rpc_audio_turn_on(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_audio_turn_off(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_audio_get_status(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_audio_set_acoustics(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_lcd_turn_on(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_lcd_turn_off(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_lcd_get_status(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_video_intercom_turn_on(cJSON *params, cJSON *id);
bk_err_t doorbell_rpc_video_intercom_turn_off(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_misc_ping(cJSON *params, cJSON *id);

bk_err_t doorbell_rpc_solution_get_config(cJSON *params, cJSON *id);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_RPC_INTERNAL_H__ */
