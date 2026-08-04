// Copyright 2024-2025 Beken
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

/*
 * Onboard-speaker audio output for the boot video player.
 * Ported / trimmed from projects/multimedia/video_player_example
 * (audio_player_device.c): raw_stream -> onboard_speaker pipeline.
 *
 * Audio output is opened only when the probed media info reports an audio
 * track (see boot_video_audio_open()); otherwise the speaker stays off.
 */

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>

#include <components/bk_audio/audio_pipeline/audio_pipeline.h>
#include <components/bk_audio/audio_streams/raw_stream.h>
#include <components/bk_audio/audio_streams/onboard_speaker_stream_v2.h>

#include "boot_video_priv.h"

/* Speaker digital gain (0x3F ~= +17dB). */
#define BOOT_VIDEO_DIG_GAIN_MAX       (0x3F)
#define BOOT_VIDEO_DIG_GAIN_DEFAULT   (0x3F)

/* Board PA control (override via Kconfig). */
#ifndef CONFIG_BOOT_VIDEO_PA_CTRL_ENABLE
#define CONFIG_BOOT_VIDEO_PA_CTRL_ENABLE  (1)
#endif
#ifndef CONFIG_BOOT_VIDEO_PA_CTRL_GPIO
#define CONFIG_BOOT_VIDEO_PA_CTRL_GPIO    (29)
#endif
#ifndef CONFIG_BOOT_VIDEO_PA_ON_LEVEL
#define CONFIG_BOOT_VIDEO_PA_ON_LEVEL     (1)
#endif
#define BOOT_VIDEO_PA_ON_DELAY_MS     (2)
#define BOOT_VIDEO_PA_OFF_DELAY_MS    (0)

typedef struct
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t  raw_stream;
    audio_element_handle_t  onboard_speaker_stream;
    uint8_t                 current_dig_gain;
    bool                    is_started;
} boot_video_audio_device_t;

static uint8_t boot_video_volume_to_dig_gain(uint8_t volume)
{
    if (volume > 100)
    {
        volume = 100;
    }
    uint32_t gain = ((uint32_t)volume * (uint32_t)BOOT_VIDEO_DIG_GAIN_MAX) / 100;
    if (gain > BOOT_VIDEO_DIG_GAIN_MAX)
    {
        gain = BOOT_VIDEO_DIG_GAIN_MAX;
    }
    return (uint8_t)gain;
}

static void boot_video_audio_device_destroy(boot_video_audio_device_t *dev)
{
    if (dev == NULL)
    {
        return;
    }

    if (dev->pipeline != NULL)
    {
        if (dev->is_started)
        {
            audio_pipeline_stop(dev->pipeline);
            audio_pipeline_wait_for_stop(dev->pipeline);
            dev->is_started = false;
        }
        audio_pipeline_unlink(dev->pipeline);
        if (dev->onboard_speaker_stream != NULL)
        {
            audio_pipeline_unregister(dev->pipeline, dev->onboard_speaker_stream);
        }
        if (dev->raw_stream != NULL)
        {
            audio_pipeline_unregister(dev->pipeline, dev->raw_stream);
        }
        audio_pipeline_deinit(dev->pipeline);
        dev->pipeline = NULL;
    }

    if (dev->onboard_speaker_stream != NULL)
    {
        audio_element_deinit(dev->onboard_speaker_stream);
        dev->onboard_speaker_stream = NULL;
    }
    if (dev->raw_stream != NULL)
    {
        audio_element_deinit(dev->raw_stream);
        dev->raw_stream = NULL;
    }

    os_free(dev);
}

avdk_err_t boot_video_audio_open(const video_player_media_info_t *media_info,
                                 uint8_t volume,
                                 bool mute,
                                 boot_video_audio_handle_t *out_handle)
{
    if (media_info == NULL || out_handle == NULL)
    {
        return AVDK_ERR_INVAL;
    }
    *out_handle = NULL;

    uint32_t channels    = media_info->audio.channels;
    uint32_t sample_rate = media_info->audio.sample_rate;
    if (channels == 0 || sample_rate == 0)
    {
        return AVDK_ERR_INVAL;
    }
    if (channels > 2)
    {
        channels = 2;
    }
    uint32_t bits = (media_info->audio.bits_per_sample > 0) ? media_info->audio.bits_per_sample : 16;

    /* frame_size = 20ms of PCM (the sink's steady pacing unit). */
    uint32_t bytes_per_sample = bits / 8;
    if (bytes_per_sample == 0)
    {
        bytes_per_sample = 2;
    }
    uint32_t samples_20ms = (sample_rate * 20 + 999) / 1000;
    uint32_t frame_size = samples_20ms * bytes_per_sample * channels;
    if (frame_size == 0)
    {
        return AVDK_ERR_INVAL;
    }

    boot_video_audio_device_t *dev = (boot_video_audio_device_t *)os_malloc(sizeof(*dev));
    if (dev == NULL)
    {
        return AVDK_ERR_NOMEM;
    }
    os_memset(dev, 0, sizeof(*dev));
    dev->current_dig_gain = boot_video_volume_to_dig_gain(volume);

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    dev->pipeline = audio_pipeline_init(&pipeline_cfg);
    if (dev->pipeline == NULL)
    {
        BOOT_VIDEO_LOGE("%s: audio_pipeline_init failed\n", __func__);
        boot_video_audio_device_destroy(dev);
        return AVDK_ERR_HWERROR;
    }

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    raw_cfg.output_port_type = PORT_TYPE_RB;
    raw_cfg.out_block_size = frame_size;
    raw_cfg.out_block_num = 2;
    dev->raw_stream = raw_stream_init(&raw_cfg);
    if (dev->raw_stream == NULL)
    {
        BOOT_VIDEO_LOGE("%s: raw_stream_init failed\n", __func__);
        boot_video_audio_device_destroy(dev);
        return AVDK_ERR_HWERROR;
    }

    onboard_speaker_stream_cfg_t spk_cfg = ONBOARD_SPEAKER_STREAM_CFG_DEFAULT();
    spk_cfg.chl_num = channels;

    aud_dac_source_t main_src = (sample_rate <= 16000) ? AUD_DAC_SOURCE_CALL : AUD_DAC_SOURCE_A2DP;
    for (int i = 0; i < AUD_DAC_SOURCE_MAX; i++)
    {
        spk_cfg.sample_rate[i] = sample_rate;
        spk_cfg.frame_size[i]  = frame_size;
    }
    spk_cfg.dac_source_bitmap = (1u << main_src);
    spk_cfg.main_dac_source   = main_src;
    spk_cfg.bits = bits;
    spk_cfg.dig_gain = dev->current_dig_gain;
#if CONFIG_BOOT_VIDEO_PA_CTRL_ENABLE
    spk_cfg.pa_ctrl_en   = true;
    spk_cfg.pa_ctrl_gpio = CONFIG_BOOT_VIDEO_PA_CTRL_GPIO;
    spk_cfg.pa_on_level  = CONFIG_BOOT_VIDEO_PA_ON_LEVEL;
    spk_cfg.pa_on_delay  = BOOT_VIDEO_PA_ON_DELAY_MS;
    spk_cfg.pa_off_delay = BOOT_VIDEO_PA_OFF_DELAY_MS;
#endif
    spk_cfg.multi_in_port_num = 0;
    spk_cfg.multi_out_port_num = 0;
    dev->onboard_speaker_stream = onboard_speaker_stream_init(&spk_cfg);
    if (dev->onboard_speaker_stream == NULL)
    {
        BOOT_VIDEO_LOGE("%s: onboard_speaker_stream_init failed\n", __func__);
        boot_video_audio_device_destroy(dev);
        return AVDK_ERR_HWERROR;
    }

    if (audio_pipeline_register(dev->pipeline, dev->raw_stream, "raw_stream") != BK_OK ||
        audio_pipeline_register(dev->pipeline, dev->onboard_speaker_stream, "onboard_speaker") != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: audio_pipeline_register failed\n", __func__);
        boot_video_audio_device_destroy(dev);
        return AVDK_ERR_HWERROR;
    }

    const char *link_tag[] = {"raw_stream", "onboard_speaker"};
    if (audio_pipeline_link(dev->pipeline, link_tag, 2) != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: audio_pipeline_link failed\n", __func__);
        boot_video_audio_device_destroy(dev);
        return AVDK_ERR_HWERROR;
    }

    if (audio_pipeline_run(dev->pipeline) != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: audio_pipeline_run failed\n", __func__);
        boot_video_audio_device_destroy(dev);
        return AVDK_ERR_HWERROR;
    }
    dev->is_started = true;

    /* Apply initial mute state (volume was baked into dig_gain above). */
    if (mute)
    {
        (void)onboard_speaker_stream_set_digital_gain(dev->onboard_speaker_stream, 0);
    }

    BOOT_VIDEO_LOGI("%s: audio output ready ch=%u rate=%u bits=%u frame=%u\n",
                    __func__, (unsigned)channels, (unsigned)sample_rate,
                    (unsigned)bits, (unsigned)frame_size);

    *out_handle = (boot_video_audio_handle_t)dev;
    return AVDK_ERR_OK;
}

void boot_video_audio_close(boot_video_audio_handle_t handle)
{
    boot_video_audio_device_destroy((boot_video_audio_device_t *)handle);
}

/* ======================================================================== *
 *  Engine audio callbacks
 * ======================================================================== */

void boot_video_audio_decode_complete_cb(void *user_data,
                                         const video_player_audio_packet_meta_t *meta,
                                         video_player_buffer_t *buffer)
{
    (void)meta;

    if (buffer == NULL || buffer->data == NULL || buffer->length == 0)
    {
        return;
    }

    boot_video_ctx_t *ctx = (boot_video_ctx_t *)user_data;
    if (ctx != NULL && ctx->audio_player_handle != NULL)
    {
        boot_video_audio_device_t *dev = (boot_video_audio_device_t *)ctx->audio_player_handle;
        if (dev->is_started)
        {
            int written = raw_stream_write(dev->raw_stream, (char *)buffer->data, (int)buffer->length);
            if (written < 0)
            {
                BOOT_VIDEO_LOGW("%s: raw_stream_write failed, ret=%d\n", __func__, written);
            }
        }
    }

    os_free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->pts = 0;
    buffer->frame_buffer = NULL;
    buffer->user_data = NULL;
}

avdk_err_t boot_video_audio_set_volume_cb(void *user_data, uint8_t volume)
{
    boot_video_ctx_t *ctx = (boot_video_ctx_t *)user_data;
    if (ctx == NULL || ctx->audio_player_handle == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    boot_video_audio_device_t *dev = (boot_video_audio_device_t *)ctx->audio_player_handle;
    uint8_t dig_gain = boot_video_volume_to_dig_gain(volume);
    bk_err_t ret = onboard_speaker_stream_set_digital_gain(dev->onboard_speaker_stream, dig_gain);
    if (ret != BK_OK)
    {
        return AVDK_ERR_HWERROR;
    }
    dev->current_dig_gain = dig_gain;
    ctx->audio_volume = volume;
    return AVDK_ERR_OK;
}

avdk_err_t boot_video_audio_set_mute_cb(void *user_data, bool mute)
{
    boot_video_ctx_t *ctx = (boot_video_ctx_t *)user_data;
    if (ctx == NULL || ctx->audio_player_handle == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    boot_video_audio_device_t *dev = (boot_video_audio_device_t *)ctx->audio_player_handle;
    uint8_t dig_gain = mute ? 0 : dev->current_dig_gain;
    bk_err_t ret = onboard_speaker_stream_set_digital_gain(dev->onboard_speaker_stream, dig_gain);
    if (ret != BK_OK)
    {
        return AVDK_ERR_HWERROR;
    }
    ctx->audio_muted = mute;
    return AVDK_ERR_OK;
}

avdk_err_t boot_video_audio_output_config_cb(void *user_data, const video_player_audio_params_t *params)
{
    /*
     * Audio output was already created for the probed media info in
     * boot_video_audio_open(); nothing to reconfigure for a single boot file.
     * Best-effort no-op so engine_open() is not failed.
     */
    (void)user_data;
    (void)params;
    return AVDK_ERR_OK;
}
