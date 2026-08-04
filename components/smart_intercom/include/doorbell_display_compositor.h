#ifndef __DOORBELL_DISPLAY_COMPOSITOR_H__
#define __DOORBELL_DISPLAY_COMPOSITOR_H__

#include <stdint.h>
#include <stdbool.h>
#include <common/bk_err.h>
#include <components/bk_gpu_ctlr.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Downlink display compositor configuration.
 *
 * The compositor drives the on-board DPU/LCD (already powered on via
 * doorbell.lcd.turnOn) with the decoded remote picture as the main surface and
 * the local ISP SP channel as a Picture-in-Picture self-view overlay.
 */
typedef struct
{
    /* Main picture: decoded remote frame geometry (GPU FLEXA input). */
    uint16_t main_width;   /**< decoded frame width  */
    uint16_t main_height;  /**< decoded frame height */

    /* Picture-in-Picture (local ISP SP self-view). */
    bool     pip_enable;
    uint16_t pip_width;    /**< ISP SP channel width  */
    uint16_t pip_height;   /**< ISP SP channel height */
    uint16_t pip_dst_x;    /**< overlay origin X on the composed surface */
    uint16_t pip_dst_y;    /**< overlay origin Y on the composed surface */
    uint16_t pip_rotate;   /**< overlay rotate degree (0/90/180/270)     */
} doorbell_compositor_config_t;

/**
 * @brief Start the compositor GPU pipeline (main GPU + optional ISP SP PIP).
 *
 * Must be called after the DPU/LCD is on. Creates the GPU controller whose
 * FLEXA input ring is @p flexa_ring so the H.264 decoder can be bonded to it.
 *
 * @param cfg              Compositor configuration.
 * @param flexa_ring       64-byte aligned FLEXA ring buffer shared with the
 *                         H.264 decoder output (owned by the caller).
 * @param flexa_buf_count  Number of FLEXA segments in @p flexa_ring.
 * @return BK_OK on success.
 */
bk_err_t doorbell_compositor_start(const doorbell_compositor_config_t *cfg,
                                   uint8_t *flexa_ring,
                                   uint8_t flexa_buf_count);

/**
 * @brief Enable the PIP self-view overlay on an already-running compositor.
 *
 * For the case where the compositor was started with pip_enable=false (the
 * local camera/ISP SP was not yet available). Starts the ISP SP capture + blit
 * overlay using the pip_* fields of @p cfg. No-op if PIP is already enabled.
 *
 * @param cfg  Config whose pip_* fields describe the overlay geometry.
 * @return BK_OK on success.
 */
bk_err_t doorbell_compositor_pip_enable(const doorbell_compositor_config_t *cfg);

/**
 * @brief Disable the PIP self-view overlay on a still-running compositor.
 *
 * Stops the ISP SP capture/blit task and clears the GPU blit overlay so the
 * small self-view window disappears (instead of freezing on the last SP frame)
 * while the main downlink picture keeps being composited. Used when the local
 * uplink/camera is turned off during a call. No-op if PIP is already off.
 *
 * @return BK_OK on success (or already off); BK_ERR_STATE if compositor is down.
 */
bk_err_t doorbell_compositor_pip_disable(void);

/** @brief Stop the compositor (PIP overlay + GPU). Safe if not running. */
void doorbell_compositor_stop(void);

/** @brief GPU controller handle for bonding the H.264 decoder; NULL if off. */
bk_gpu_ctlr_handle_t doorbell_compositor_gpu_handle_get(void);

/** @brief Whether the compositor pipeline is currently running. */
bool doorbell_compositor_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_DISPLAY_COMPOSITOR_H__ */
