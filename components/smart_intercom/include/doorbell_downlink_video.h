#ifndef __DOORBELL_DOWNLINK_VIDEO_H__
#define __DOORBELL_DOWNLINK_VIDEO_H__

#include <stdint.h>
#include <stdbool.h>
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Downlink H.264 receive configuration (from
 *        doorbell.imageStream.setReceiveConfig).
 */
typedef struct
{
    uint16_t width;         /**< decoded frame width           */
    uint16_t height;        /**< decoded frame height          */
    uint16_t fps;           /**< nominal frame rate (hint)     */
    uint16_t p_frame_count; /**< GOP P-frame count (hint)      */
} doorbell_downlink_h264_config_t;

/**
 * @brief Configure and (re)start the downlink H.264 receive+decode+display path.
 *
 * Idempotent for an identical config. A different config restarts the pipeline.
 * The DPU/LCD must already be on (doorbell.lcd.turnOn). PIP self-view is enabled
 * automatically when the ISP is running with an SP channel.
 *
 * @param cfg  H.264 receive configuration.
 * @return BK_OK on success.
 */
bk_err_t doorbell_downlink_set_h264_receive_config(const doorbell_downlink_h264_config_t *cfg);

/** @brief Stop and tear down the downlink pipeline. Safe if not running. */
bk_err_t doorbell_downlink_video_stop(void);

/**
 * @brief Enable the PIP self-view overlay on an already-running compositor.
 *
 * Used when the downlink pipeline was started before the local camera: the
 * compositor comes up with PIP off (no ISP SP source yet). Once the camera is
 * turned on, this attaches the local ISP SP self-view to the running compositor.
 * No-op / BK_ERR_STATE if downlink is not running or the ISP is not up.
 *
 * @return BK_OK on success.
 */
bk_err_t doorbell_downlink_pip_enable(void);

/**
 * @brief Disable the PIP self-view overlay when the local camera/uplink is off.
 *
 * Counterpart to doorbell_downlink_pip_enable(). Stops blitting the local ISP SP
 * self-view and clears the small window so it no longer shows the last stale SP
 * frame; the downlink main picture keeps running. No-op / BK_ERR_STATE if the
 * compositor is not running.
 *
 * @return BK_OK on success.
 */
bk_err_t doorbell_downlink_pip_disable(void);

/** @brief Whether the downlink decode pipeline is running. */
bool doorbell_downlink_video_is_running(void);

/**
 * @brief Producer lost an access unit because no decode slot was free.
 *
 * Marks the H.264 reference chain broken (same effect as a decode failure) so
 * the decode task enters wait-for-IDR instead of feeding the next P frame
 * against a missing reference (macroblock mosaic on motion regions).
 */
void doorbell_downlink_video_notify_ref_break(void);

/**
 * @brief Network video-channel producer: submit one received H.264 access unit.
 *
 * Mirrors the uplink direction: one call carries one complete encoded frame,
 * which is copied into a free downlink buffer slot and queued for the decoder.
 *
 * @param data    Encoded H.264 access-unit bytes.
 * @param length  Byte length.
 * @return BK_OK (frames are dropped silently under back-pressure).
 */
int doorbell_downlink_video_recv(uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_DOWNLINK_VIDEO_H__ */
