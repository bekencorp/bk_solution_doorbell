#ifndef _DOORBELL_ISP_SP_H_
#define _DOORBELL_ISP_SP_H_

#include <common/bk_include.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the ISP SP channel for continuous NV12 self-view capture.
 *
 * The SP channel is NOT started by the board sp_enable flag alone; it must be
 * opened explicitly and, because the MP channel is already streaming (uplink
 * encode), the open is synchronized to an MP mid-frame interrupt to avoid a
 * capture race (mirrors the software-snapshot SP acquisition path).
 *
 * Safe to call when the channel is already open (returns BK_OK, reuses it).
 *
 * @param width  SP capture width  (must match board isp.sp_width).
 * @param height SP capture height (must match board isp.sp_height).
 * @return BK_OK on success.
 */
int doorbell_isp_sp_open(uint16_t width, uint16_t height);

/**
 * @brief Read one SP NV12 frame into @p frame (direct channel pop_buf + copy).
 *
 * @param frame      Destination buffer (>= width*height*3/2).
 * @param size       Destination buffer size in bytes.
 * @param timeout_ms Wait timeout for the next SP frame.
 * @return BK_OK on success, BK_ERR_* / timeout otherwise.
 */
int doorbell_isp_sp_read_nv12(uint8_t *frame, uint32_t size, uint32_t timeout_ms);

/**
 * @brief Close the SP channel opened by doorbell_isp_sp_open(). No-op if not open.
 */
void doorbell_isp_sp_close(void);

#ifdef __cplusplus
}
#endif

#endif /* _DOORBELL_ISP_SP_H_ */
