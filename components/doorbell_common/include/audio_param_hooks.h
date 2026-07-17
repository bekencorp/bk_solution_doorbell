#ifndef __AUDIO_PARAM_HOOKS_H__
#define __AUDIO_PARAM_HOOKS_H__

#include <stdint.h>

#if CONFIG_AUD_PARAM_CTRL
/*
 * Audio param-ctrl / debug-tool hooks.
 *
 * Contract between the (shared) audio-device lifecycle code that calls these and
 * the per-project implementation in audio_param/audio_para.c. bind exposes the
 * running voice service and its default presets to the debug tool (EQ preset
 * selected by speaker sample rate) and applies them; unbind releases them on
 * stop. Both the caller and the implementer include this header so the
 * signatures stay in sync.
 */
void media_audio_param_bind_voc_handle(void *voc_handle, uint32_t sample_rate);

void media_audio_param_unbind_voc_handle(void);
#endif

#endif /* __AUDIO_PARAM_HOOKS_H__ */
