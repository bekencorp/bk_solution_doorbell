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

#ifndef __DOORBELL_RINGTONE_H__
#define __DOORBELL_RINGTONE_H__

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Play the local incoming-call ringtone on the onboard speaker.
 * Safe to call from the call worker thread. Idempotent: a playing ringtone
 * is restarted. Must be stopped before doorbell_audio_turn_on() (the call
 * voice path owns the DAC).
 */
bk_err_t doorbell_ringtone_start(void);

/**
 * Stop the ringtone and release the speaker pipeline. Idempotent.
 * Must run on the call worker (not from the feeder thread).
 */
void doorbell_ringtone_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_RINGTONE_H__ */
