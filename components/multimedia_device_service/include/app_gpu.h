// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <components/bk_gpu_types.h>
#include "app_gpu_types.h"
#include "avdk_error.h"


/**
 * @brief Turn on GPU with optional parameters
 * @param gpu_in_w: GPU input width (ISP output width), 0 to use default from gpu_board_config
 * @param gpu_in_h: GPU input height (ISP output height), 0 to use default from gpu_board_config
 * @param gpu_out_w: GPU output width, 0 to use default from gpu_board_config
 * @param gpu_out_h: GPU output height, 0 to use default from gpu_board_config
 * @param rotate: Rotation angle (0/90/180/270), 0xFF to use default from gpu_board_config
 * @return AVDK_ERR_OK on success, error code otherwise
 */
avdk_err_t app_gpu_turn_on(gpu_board_config_t *config);

avdk_err_t app_gpu_turn_off(bk_gpu_ctlr_handle_t ctlr);

avdk_err_t app_gpu_v2_turn_on(uint16_t width, uint16_t height);

int app_gpu_board_config_set(gpu_board_config_t *config);
gpu_board_config_t *app_gpu_board_config_get(void);
bk_gpu_ctlr_handle_t app_gpu_handle_get(void);


#ifdef __cplusplus
}
#endif