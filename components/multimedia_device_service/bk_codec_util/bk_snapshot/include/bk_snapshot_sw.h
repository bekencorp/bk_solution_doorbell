// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdint.h>
#include <components/avdk_utils/avdk_error.h>
#include <components/bk_isp_camera_types.h>
#include "bk_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BK_SNAPSHOT_SW_DEFAULT_WIDTH   (640U)
#define BK_SNAPSHOT_SW_DEFAULT_HEIGHT  (480U)
#define BK_SNAPSHOT_SW_DEFAULT_QUALITY (80U)
#define BK_SNAPSHOT_SW_DEFAULT_TIMEOUT_MS (3000U)

typedef struct {
	bk_isp_camera_ctlr_handle_t camera_handle;
	uint16_t width;
	uint16_t height;
	uint8_t jpeg_quality;
	uint32_t read_timeout_ms;
} bk_snapshot_sw_config_t;

avdk_err_t bk_snapshot_sw_capture(bk_snapshot_sw_config_t *config,
				  bk_snapshot_image_t *out_image);

avdk_err_t bk_snapshot_sw_prepare(void);

avdk_err_t bk_snapshot_sw_sp_channel_open_at_mp_mid_frame(bk_isp_camera_ctlr_handle_t camera,
							uint16_t width,
							uint16_t height);

avdk_err_t bk_snapshot_sw_deinit(void);

#ifdef __cplusplus
}
#endif
