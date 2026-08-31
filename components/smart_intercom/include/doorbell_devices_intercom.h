#ifndef __DOORBELL_DEVICES_INTERCOM_H__
#define __DOORBELL_DEVICES_INTERCOM_H__

#include <common/bk_include.h>
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Intercom-only extensions to the devices module.
 *
 * These entry points are implemented by smart_intercom/src/doorbell_devices.c
 * (the video-intercom fork of the devices layer) and consumed only by
 * smart_intercom code (downlink video, PIP compositor, self-test). They are
 * deliberately kept OUT of doorbell_common/include/doorbell_devices.h so the
 * common layer carries no intercom-specific API: smart_lock and the legacy
 * projects never see or need them.
 */

/**
 * @brief Get the active ISP controller handle (owned by the capture path).
 *
 * Used by the downlink PIP compositor to read the ISP SP channel for the
 * local self-view overlay while the MP channel feeds the uplink encoder.
 *
 * @return ISP handle, or NULL if the camera/ISP is not running.
 */
void *doorbell_devices_isp_handle_get(void);

/**
 * @brief Whether the uplink camera + H.264 encode path is active.
 *
 * When true alongside 720p downlink decode, PSRAM is tight (decoder recon pool
 * + uplink encoder + PIP SP). Used to defer PIP until the first decoded frame.
 */
bool doorbell_devices_uplink_active(void);

/**
 * @brief Detach the single-view preview GPU bond (ISP MP -> GPU -> LCD).
 *
 * Used when entering the intercom/downlink mode: the full-screen local preview
 * releases the GPU (and its HSRAM FLEXA ring) so the downlink compositor can
 * own the GPU and composite the decoded main picture + ISP SP PIP. The uplink
 * ISP MP -> H264 encode bond is left running.
 *
 * @return BK_OK on success (also OK if nothing was attached).
 */
int doorbell_devices_preview_gpu_detach(void);

/**
 * @brief Re-attach the single-view preview GPU bond.
 *
 * Used when leaving the downlink mode to restore the full-screen local preview.
 * No-op unless both camera and LCD are on.
 *
 * @return BK_OK on success.
 */
int doorbell_devices_preview_gpu_attach(void);

/**
 * @brief Hold the single-view preview GPU off during a pending intercom bring-up.
 *
 * videoIntercom.turnOn opens uplink before downlink; attaching the preview GPU
 * (ISP MP -> GPU) and then tearing it down for the compositor can leave HSRAM
 * too fragmented for the compositor's ping-pong buffers (main picture green).
 * While held, doorbell_camera_turn_on skips gpu_pipeline_attach; the downlink
 * compositor owns the GPU from the start.
 *
 * @param hold  true to defer preview GPU; false to restore default behaviour.
 */
void doorbell_devices_preview_gpu_hold(bool hold);

/**
 * @brief Set the uplink sensor fps requested for the next camera turn-on.
 *
 * The App-negotiated uplink fps used to travel inside camera_parameters_t.fps,
 * but that struct lives in the shared doorbell_common header consumed by
 * smart_lock and the legacy projects (smart_lock even length-checks
 * sizeof(camera_parameters_t) against the wire command). Carrying the fps here,
 * intercom-private, keeps the common struct/ABI untouched. Consumed exactly once
 * by the next doorbell_camera_turn_on(); 0 means "use the board default".
 *
 * @param fps  requested sensor fps, or 0 for the board default.
 */
void doorbell_devices_set_uplink_fps(uint16_t fps);

/**
 * @brief Force the uplink H.264 encoder to emit an IDR (keyframe) next frame.
 *
 * Used by the self-test loopback so the downlink decoder, which may arm long
 * after the encoder started (missing the initial IDR) or lose its reference
 * chain under GPU/PIP contention, can (re)establish a keyframe reference.
 *
 * @return BK_OK on success, BK_FAIL if the encoder is not running.
 */
bk_err_t doorbell_devices_force_idr(void);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_DEVICES_INTERCOM_H__ */
