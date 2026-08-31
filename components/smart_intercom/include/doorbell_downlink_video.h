#ifndef __DOORBELL_DOWNLINK_VIDEO_H__
#define __DOORBELL_DOWNLINK_VIDEO_H__

#include <stdint.h>
#include <stdbool.h>
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compile-time downlink target resolution (Kconfig choice
 * SMART_INTERCOM_DL_RES_720P / _1080P, default 720p). The runtime decode
 * geometry still comes from the App via doorbell.imageStream.setReceiveConfig;
 * these macros only drive the advertised getConfig default template and the
 * resolution-dependent buffer sizing so the selected resolution is supported.
 */
#if defined(CONFIG_SMART_INTERCOM_DL_RES_1080P)
#define DOORBELL_DL_MAIN_WIDTH   1920U
#define DOORBELL_DL_MAIN_HEIGHT  1080U
#else
/* Landscape 1280x720 downlink. The GPU compositor scales to 1920x1080 and
 * rotates 90deg onto the 1080x1920 panel. Rotation is mandatory here: the panel
 * width 1080 is NOT 16-aligned, so a non-rotated portrait output would be forced
 * to a 1088-wide compressed buffer whose scan-out stride no longer matches the
 * panel -> "DC panel0 data underflow" -> pipeline stall (see git history). The
 * rotated pipeline uses the 16-aligned 1920 dimension as the buffer width and
 * scans out cleanly. */
#define DOORBELL_DL_MAIN_WIDTH   1280U
#define DOORBELL_DL_MAIN_HEIGHT  720U
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
 * @brief Hint that uplink capture/encode will share HSRAM with the downlink.
 *
 * Set before doorbell_downlink_set_h264_receive_config() in videoIntercom so
 * the FLEXA ring depth budgets room for the compositor GPU ping-pong even when
 * the uplink has not been opened yet.
 */
void doorbell_downlink_set_concurrent_uplink(bool expect);

/**
 * @brief Release HSRAM reserved for the compositor GPU ping-pong.
 *
 * Called by the display compositor immediately before bk_gpu_open() so the SDK
 * allocator can claim the same contiguous block.
 */
void doorbell_downlink_release_pingpong_hold(void);

/**
 * @brief Producer lost an access unit because no decode slot was free.
 *
 * Marks the H.264 reference chain broken (same effect as a decode failure) so
 * the decode task enters wait-for-IDR instead of feeding the next P frame
 * against a missing reference (macroblock mosaic on motion regions).
 */
void doorbell_downlink_video_notify_ref_break(void);

/**
 * @brief Statistics hook: one complete H.264 access unit was received over WiFi.
 *
 * Called from the ready-queue producer (doorbell_downlink_ready_push), i.e. the
 * single chokepoint shared by both the copy path (doorbell_downlink_video_recv)
 * and the zero-copy path (doorbell_downlink_slot_fb_commit). Feeds the 1s WiFi
 * receive frame-rate / bitrate counters printed by the downlink stats timer.
 *
 * @param bytes  Access-unit byte length.
 */
void doorbell_downlink_video_stats_on_recv(uint32_t bytes);

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
