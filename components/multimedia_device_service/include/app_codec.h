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

#include <components/bk_encode/bk_h264_encode_types.h>

#ifdef __cplusplus
extern "C" {
#endif

int app_h264e_turn_off(void);
int app_h264e_turn_on(void);

bk_err_t app_h264_encode_open(uint16_t width, uint16_t height);
bk_err_t app_h264_encode_close(void);

bk_err_t app_h264_encode_get_handle(bk_h264_encode_ctlr_handle_t *handle);

void *app_h264_encode_handle_get(void);

#ifdef __cplusplus
}
#endif