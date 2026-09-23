#ifndef __LCD_OSD_H__
#define __LCD_OSD_H__

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-shot product LCD OSD (time + WiFi icon) for video_intercom.
 *
 * Registers the compositor GPU attach/detach callback. Overlay is created
 * only while the downlink compositor GPU is open (videoIntercom.turnOn);
 * camera.turnOn has no compositor and therefore no OSD.
 * UART: `osd on` / `osd off` / `osd` (status). Default is on.
 */
bk_err_t lcd_osd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_OSD_H__ */
