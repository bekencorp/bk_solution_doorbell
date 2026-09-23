// Copyright 2020-2025 Beken
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

#ifndef __DOORBELL_UI_I18N_H__
#define __DOORBELL_UI_I18N_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Designer dropdown: 0 = 中文, 1 = English. Generated pages bake Chinese
 * strings; this layer overlays the active language after init/bind. */
#define DB_UI_LANG_ZH  0
#define DB_UI_LANG_EN  1

uint8_t doorbell_ui_lang(void);
void doorbell_ui_set_lang(uint8_t lang);

/* Call on the LVGL thread. Safe if some pages have not been created yet. */
void doorbell_ui_apply_lang(void);

const char *doorbell_ui_str_lock(bool unlocked);
const char *doorbell_ui_str_prov_title(void);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_UI_I18N_H__ */
