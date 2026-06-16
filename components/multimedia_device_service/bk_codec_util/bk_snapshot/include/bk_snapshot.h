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
#include <components/bk_encode/bk_h264_encode_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bk_snapshot_free_fn)(void *data, void *user_data);

typedef struct {
	void *data;
	uint32_t size;
	uint32_t width;
	uint32_t height;
	bk_snapshot_free_fn free_fn;
	void *free_arg;
} bk_snapshot_image_t;

typedef struct {
	void *source_handle;
	bk_h264_encode_ctlr_handle_t h264_handle;
	void **h264_bond;
	uint8_t jpeg_quality;
	uint32_t timeout_ms;
} bk_snapshot_config_t;

avdk_err_t bk_snapshot_capture(bk_snapshot_config_t *config,
				   bk_snapshot_image_t *out_image);

void bk_snapshot_image_release(bk_snapshot_image_t *image);

avdk_err_t bk_snapshot_deinit(void);

#ifdef __cplusplus
}
#endif
