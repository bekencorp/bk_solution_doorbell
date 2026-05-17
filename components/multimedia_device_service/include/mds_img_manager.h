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

bk_err_t bk_encoded_data_manager_init(void);

bk_err_t bk_encoded_data_manager_deinit(uint8_t input);

void *bk_encoded_data_request(void);

bk_err_t bk_encoded_data_complete_request(uint8_t *frame);

bk_err_t bk_encoded_data_free_request(uint8_t *frame);

void *bk_encoded_complete_data_request(uint32_t timeout_ms);

bk_err_t bk_encoded_complete_data_free_request(uint8_t *frame);

#ifdef __cplusplus
}
#endif
