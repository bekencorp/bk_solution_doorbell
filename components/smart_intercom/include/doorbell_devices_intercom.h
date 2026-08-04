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
