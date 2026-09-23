/*
 * Project-level LVGL config override for video_intercom.
 *
 * The lvgl component (ap/components/lvgl/CMakeLists.txt) sets
 *   LV_CONF_PATH="<project>/ap/lv_conf_custom.h"
 * when this file exists, so LVGL includes THIS file as its whole config.
 *
 * We inherit every tuned default from the SDK's lv_conf.h (found on the lvgl
 * component include path) and only flip the options this project needs on top,
 * so we never fork/duplicate the ~1000-line SDK config and never touch the SDK.
 *
 * Enabled here: LV_USE_QRCODE (the SDK default is 0) for the QR-code based BLE
 * provisioning UI. LV_USE_CANVAS (a lv_qrcode dependency) is already 1 in the
 * SDK config.
 */
#ifndef LV_CONF_CUSTOM_H
#define LV_CONF_CUSTOM_H

/* Pull in all of the SDK's tuned LVGL defaults (color depth, memory, GPU, fonts
 * ...). Resolves to ap/components/lvgl/lvgl_v9/lv_conf.h via the lvgl component
 * public include dirs. */
#include "lv_conf.h"

/* --- project overrides on top of the SDK defaults --- */
#ifdef LV_USE_QRCODE
#undef LV_USE_QRCODE
#endif
#define LV_USE_QRCODE 1

/* Bigger English fonts for the provisioning title / hint on the 1080px panel.
 * The font .c files are always compiled by the lvgl component; these macros just
 * make the symbols available. Doubled from the previous 20/28: title uses 48
 * (the largest built-in montserrat), hint/status use 40. */
#ifdef LV_FONT_MONTSERRAT_40
#undef LV_FONT_MONTSERRAT_40
#endif
#define LV_FONT_MONTSERRAT_40 1

#ifdef LV_FONT_MONTSERRAT_48
#undef LV_FONT_MONTSERRAT_48
#endif
#define LV_FONT_MONTSERRAT_48 1

#endif /* LV_CONF_CUSTOM_H */
