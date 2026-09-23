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

    /** When true, skip the 3-buffer UNCODED output pool during @ref
     * doorbell_compositor_start and call @ref doorbell_compositor_out_pool_init
     * after the H.264 decoder opens. Concurrent uplink + downlink intercom needs
     * this: decoder open only needs ~4KB UNCODED and must run before the ~6MB
     * pool is reserved. */
    bool     defer_out_pool;

    /** When true, stop after @c bk_gpu_init and call @ref doorbell_compositor_gpu_open
     * after the H.264 decoder opens (and after the deferred output pool, if any).
     * Concurrent intercom needs this so @c bk_gpu_open does not grab a ~2MB UNCODED
     * dpu_frame_buffer before @c vcdec_h264_open runs. */
    bool     defer_gpu_open;
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
 * @brief Allocate the compositor GPU output pool deferred by defer_out_pool.
 *
 * Must be called after the H.264 decoder is open when defer_out_pool was set.
 * No-op if the pool was already initialized during start.
 */
bk_err_t doorbell_compositor_out_pool_init(void);

/**
 * @brief Finish compositor startup deferred by defer_gpu_open.
 *
 * Releases the HSRAM ping-pong hold, calls @c bk_gpu_open, and starts PIP if
 * configured. Must run after the H.264 decoder (and deferred output pool) are up.
 */
bk_err_t doorbell_compositor_gpu_open(void);

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

/**
 * @brief GPU attach/detach callback for product LCD OSD (time / WiFi).
 *
 * @p attached true  after a successful @c bk_gpu_open (GPU is usable).
 * @p attached false before @c bk_gpu_close, while the handle is still valid.
 * Existing start/stop/PIP signatures are unchanged; this is additive.
 */
typedef void (*doorbell_compositor_gpu_event_cb_t)(bk_gpu_ctlr_handle_t gpu, bool attached);

/** @brief Register (or clear with NULL) the GPU attach/detach callback. */
void doorbell_compositor_set_gpu_event_cb(doorbell_compositor_gpu_event_cb_t cb);

#if CONFIG_DOOR_ACCESS_CALL_FLOW
/** Max sprites accepted by @ref doorbell_compositor_osd_show_n in one call. */
#define DOORBELL_OSD_SPRITE_MAX  (4U)

/** One ARGB8888 overlay sprite in panel coordinates. */
typedef struct
{
    /** Stable element name, e.g. "lock". Identifies the sprite across calls so
     * an unchanged sprite keeps the layer it is already composited into. */
    const char *name;
    const void *argb8888; /**< width * height * 4 bytes; copied into the engine */
    uint16_t    width;
    uint16_t    height;
    uint16_t    dst_x;    /**< top-left X on the panel display surface */
    uint16_t    dst_y;    /**< top-left Y on the panel display surface */
    /** Set when the pixels behind @ref argb8888 were rewritten in place (the
     * pointer alone cannot show that). A changed pointer or geometry is
     * detected automatically. */
    bool        dirty;
} doorbell_osd_sprite_t;

/**
 * @brief Show a translucent OSD sprite (e.g. the hang-up button) on the composed
 *        downlink picture via the GPU controller's shared overlay layer.
 *
 * Indoor-display call-flow only: implemented with the public bk_draw_osd controller bound to
 * this compositor's GPU. Compiled out unless CONFIG_DOOR_ACCESS_CALL_FLOW
 * (which also disables PIP, so the shared overlay is free).
 *
 * The ARGB8888 pixels are copied into an engine sprite, so the caller's
 * @p argb8888 buffer may be freed on return. The sprite is alpha-blended onto
 * every composed output frame until @ref doorbell_compositor_osd_hide (or the
 * compositor stops). Calling again replaces the previous sprite.
 *
 * @param name      Stable element name (see @ref doorbell_osd_sprite_t).
 * @param argb8888  Source ARGB8888 pixels (@p width * @p height * 4 bytes).
 * @param width     Sprite width in pixels.
 * @param height    Sprite height in pixels.
 * @param dst_x     Top-left X on the panel display surface.
 * @param dst_y     Top-left Y on the panel display surface.
 * @param rotate    Sprite rotation onto the display (0/90/180/270).
 * @return BK_OK on success; BK_ERR_STATE if the compositor is not running.
 */
bk_err_t doorbell_compositor_osd_show(const char *name, const void *argb8888,
                                      uint16_t width, uint16_t height,
                                      uint16_t dst_x, uint16_t dst_y, uint16_t rotate);

/**
 * @brief Show / refresh @p count OSD sprites in one shot (hang-up + lock + timer).
 *
 * @p sprites is the complete element set, so the same array is resubmitted on
 * every refresh. Only the elements whose pixels or placement changed are
 * re-composited; the others keep the overlay layer they are already blitted
 * from, and a re-composited element is swapped at the next frame boundary. That
 * is what keeps a once-a-second timer update from blinking the whole OSD: a full
 * repaint would leave every layer unpainted for the frames composed while it
 * runs.
 *
 * Nearby sprites may be clustered into one GPU overlay layer; distant sprites
 * use separate layers. @p count is capped at @ref DOORBELL_OSD_SPRITE_MAX.
 * Changing the element set (different names or count) rebuilds the OSD instance
 * and does repaint everything, so keep the set stable across a call.
 */
bk_err_t doorbell_compositor_osd_show_n(const doorbell_osd_sprite_t *sprites, uint8_t count,
                                        uint16_t rotate);

/** @brief Remove the OSD sprite shown by @ref doorbell_compositor_osd_show. */
bk_err_t doorbell_compositor_osd_hide(void);
#endif /* CONFIG_DOOR_ACCESS_CALL_FLOW */

/**
 * @brief HSRAM ping-pong strip size the compositor GPU needs (compressed flexa path).
 *
 * Mirrors bk_gpu_ctlr_default.c gpu_flex_init_pingpong_buffer() so the downlink
 * layer can budget HSRAM before allocating the FLEXA decode ring.
 */
uint32_t doorbell_compositor_gpu_pingpong_bytes(uint16_t dst_w, uint16_t dst_h);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_DISPLAY_COMPOSITOR_H__ */
