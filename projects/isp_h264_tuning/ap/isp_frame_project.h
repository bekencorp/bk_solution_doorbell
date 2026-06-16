#pragma once

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t isp_frame_project_server_start(uint16_t width, uint16_t height, uint16_t fps);
bk_err_t isp_frame_project_server_stop(void);
bk_err_t isp_frame_project_module_init(uint16_t width, uint16_t height, uint16_t fps);
bk_err_t isp_frame_project_module_stop(void);
int isp_frame_project_cli_init(void);

#ifdef __cplusplus
}
#endif
