#pragma once

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* H264E PC module: unified preview server owns network; this module owns encode. */
bk_err_t h264e_stream_project_server_start(uint16_t width, uint16_t height, uint16_t fps);
bk_err_t h264e_stream_project_server_stop(void);
bk_err_t h264e_stream_project_module_init(uint16_t width, uint16_t height, uint16_t fps);
bk_err_t h264e_stream_project_module_stop(void);
bk_err_t h264e_stream_project_encode_start(void);
bk_err_t h264e_stream_project_encode_stop(void);

/* Backward-compatible aliases for server start/stop. */
bk_err_t h264e_stream_project_start(uint16_t width, uint16_t height, uint16_t fps);
bk_err_t h264e_stream_project_stop(void);

int h264e_stream_project_cli_init(void);

#ifdef __cplusplus
}
#endif
