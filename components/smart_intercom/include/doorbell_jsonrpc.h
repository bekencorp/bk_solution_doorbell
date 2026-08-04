#ifndef __DOORBELL_JSONRPC_H__
#define __DOORBELL_JSONRPC_H__

#include <stdint.h>
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse and dispatch one complete JSON-RPC 2.0 control message.
 *
 * The control channel is framed by the common ntwk_json layer
 * (CONFIG_NTWK_CTRL_CHAN_JSON), so @p json is already reassembled into a
 * complete, NUL-terminated JSON text before this function is called.
 *
 * @param json    Complete JSON-RPC 2.0 text (NUL-terminated).
 * @param length  Byte length of the text (excluding the trailing NUL).
 */
void doorbell_jsonrpc_handle_cmd(const char *json, uint32_t length);

/**
 * @brief Send a device-originated JSON-RPC notification (no id).
 *
 * @param method  Notification method name, e.g. "doorbell.notify.heartbeat".
 * @param params  Optional params object; ownership is transferred (deleted by
 *                the callee). May be NULL for no params.
 * @return BK_OK on success.
 */
bk_err_t doorbell_jsonrpc_send_notify(const char *method, void *params);

/**
 * @brief Notify the App to force an immediate IDR (key frame) after the
 *        downlink decoder detects frame loss / a broken reference.
 *
 * Sends the device-originated JSON-RPC notification
 * "doorbell.notify.requestKeyFrame" with params { reason, imageFormat }
 * The caller is responsible for rate-limiting to avoid
 * an IDR-request storm.
 *
 * @param reason        Trigger reason, e.g. "frameLoss" or "decodeError".
 * @param image_format  Downlink image format, e.g. "h264".
 * @return BK_OK on success.
 */
bk_err_t doorbell_notify_request_keyframe(const char *reason, const char *image_format);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_JSONRPC_H__ */
