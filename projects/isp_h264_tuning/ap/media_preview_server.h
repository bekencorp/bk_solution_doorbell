#pragma once

#include <stdint.h>
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t media_preview_server_start(uint16_t width, uint16_t height, uint16_t fps);
bk_err_t media_preview_server_stop(void);
int media_preview_server_cli_init(void);

#ifdef __cplusplus
}
#endif
