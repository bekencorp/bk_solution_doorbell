#ifndef __OSD_CALL_TIMER_H__
#define __OSD_CALL_TIMER_H__

#include <stdint.h>
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** ARGB8888 strip for the in-call duration OSD (GPU overlay). Width is 64-byte aligned. */
#define OSD_CALL_TIMER_W  (256)
#define OSD_CALL_TIMER_H  (48)

bk_err_t osd_call_timer_acquire(void);
void osd_call_timer_release(void);
void osd_call_timer_paint(uint32_t elapsed_sec);
const uint32_t *osd_call_timer_argb(void);

#ifdef __cplusplus
}
#endif

#endif /* __OSD_CALL_TIMER_H__ */
