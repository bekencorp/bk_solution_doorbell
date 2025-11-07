#ifndef __YUV_DISPLAY_H__
#define __YUV_DISPLAY_H__

#include <common/bk_include.h>
#include <components/bk_display.h>
#include "doorbell_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief YUV Direct Display Module
 *
 * This module handles YUV data directly output from camera, supports hardware
 * rotation and direct display to LCD without encoding/decoding, improving performance.
 *
 * Use cases:
 * - DVP camera outputs MJPEG + YUV mode (MJPEG for network transmission, YUV for local display)
 * - DVP camera outputs H264 + YUV mode (H264 for network transmission, YUV for local display)
 * - UVC camera outputs YUV mode (future extension)
 */

/**
 * @brief Open YUV display function
 *
 * Create YUV processing task, initialize hardware rotation module (if needed), register YUV consumer.
 *
 * @param info Device information, including camera type, transfer format, etc.
 * @param rotate_angle Rotation angle (0/90/180/270)
 *
 * @return bk_err_t
 *         - BK_OK: Success
 *         - BK_ERR_NO_MEM: Out of memory
 *         - Other: Initialization failed
 */
bk_err_t yuv_display_open(db_device_info_t *info, int rotate_angle);

/**
 * @brief Close YUV display function
 *
 * Stop YUV processing task, unregister YUV consumer, release all resources.
 *
 * @return bk_err_t
 *         - BK_OK: Success
 *         - BK_FAIL: Failed
 */
bk_err_t yuv_display_close(void);

/**
 * @brief Check if YUV display task is running
 *
 * @return true: Running
 *         false: Not running
 */
bool yuv_display_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __YUV_DISPLAY_H__ */
