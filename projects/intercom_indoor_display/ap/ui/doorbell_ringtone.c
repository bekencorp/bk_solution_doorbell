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

/*
 * Local incoming-call ringtone: dual-tone (440 Hz + 480 Hz, ITU-T E.180
 * ringback) with 1 s on / 2 s off cadence, generated as 16 kHz / 16-bit / mono
 * PCM and written to raw_stream -> onboard_speaker. No encoded asset in flash.
 *
 * The pipeline is torn down on stop so doorbell_audio_turn_on() can take the
 * DAC for the two-way call. Cadence / generation run on a dedicated feeder
 * thread; start/stop are serialized by the call worker.
 */

#include "doorbell_ringtone.h"

#include <common/bk_include.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/os.h>
#include <math.h>
#include <string.h>

#define TAG "db-ring"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#if CONFIG_DOOR_ACCESS_CALL_FLOW

#include <components/bk_audio/audio_pipeline/audio_pipeline.h>
#include <components/bk_audio/audio_streams/raw_stream.h>
#if CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2
#include <components/bk_audio/audio_streams/onboard_speaker_stream_v2.h>
#else
#include <components/bk_audio/audio_streams/onboard_speaker_stream.h>
#endif

#define RING_SAMPLE_RATE     (16000U)
#define RING_CHANNELS        (1U)
#define RING_BITS            (16U)
#define RING_FRAME_MS        (20U)
#define RING_ON_MS           (1000U)
#define RING_OFF_MS          (2000U)
#define RING_F1_HZ           (440.0f)
#define RING_F2_HZ           (480.0f)
#define RING_AMPLITUDE       (0.28f)   /* per tone; dual-tone peak ~0.56 FS */
#define RING_FEEDER_STACK    (3 * 1024)
#define RING_FEEDER_PRIO     (4)

#ifndef CONFIG_BOOT_VIDEO_PA_CTRL_ENABLE
#define CONFIG_BOOT_VIDEO_PA_CTRL_ENABLE  (0)
#endif
#ifndef CONFIG_BOOT_VIDEO_PA_CTRL_GPIO
#define CONFIG_BOOT_VIDEO_PA_CTRL_GPIO    (29)
#endif
#ifndef CONFIG_BOOT_VIDEO_PA_ON_LEVEL
#define CONFIG_BOOT_VIDEO_PA_ON_LEVEL     (1)
#endif

typedef struct
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t  raw_stream;
    audio_element_handle_t  speaker;
    beken_thread_t          feeder;
    beken_semaphore_t       feeder_done;
    volatile bool           feed_run;
    bool                    open;
} ringtone_dev_t;

static ringtone_dev_t s_ring;

static uint32_t ring_frame_bytes(void)
{
    return (RING_SAMPLE_RATE * RING_FRAME_MS / 1000U) * (RING_BITS / 8U) * RING_CHANNELS;
}

static void ringtone_pipeline_destroy(ringtone_dev_t *dev)
{
    if (dev == NULL)
    {
        return;
    }

    if (dev->pipeline != NULL)
    {
        audio_pipeline_stop(dev->pipeline);
        audio_pipeline_wait_for_stop(dev->pipeline);
        audio_pipeline_unlink(dev->pipeline);
        if (dev->speaker != NULL)
        {
            audio_pipeline_unregister(dev->pipeline, dev->speaker);
        }
        if (dev->raw_stream != NULL)
        {
            audio_pipeline_unregister(dev->pipeline, dev->raw_stream);
        }
        audio_pipeline_deinit(dev->pipeline);
        dev->pipeline = NULL;
    }

    if (dev->speaker != NULL)
    {
        audio_element_deinit(dev->speaker);
        dev->speaker = NULL;
    }
    if (dev->raw_stream != NULL)
    {
        audio_element_deinit(dev->raw_stream);
        dev->raw_stream = NULL;
    }
}

static void ringtone_feeder_thread(void *arg)
{
    ringtone_dev_t *dev = (ringtone_dev_t *)arg;
    const uint32_t frame_bytes = ring_frame_bytes();
    const uint32_t frame_samples = frame_bytes / 2U;
    const uint32_t on_frames = RING_ON_MS / RING_FRAME_MS;
    const uint32_t period_frames = (RING_ON_MS + RING_OFF_MS) / RING_FRAME_MS;
    const float two_pi = 6.28318530718f;
    const float step1 = two_pi * RING_F1_HZ / (float)RING_SAMPLE_RATE;
    const float step2 = two_pi * RING_F2_HZ / (float)RING_SAMPLE_RATE;
    float phase1 = 0.0f;
    float phase2 = 0.0f;
    uint32_t frame_idx = 0;
    int16_t *pcm = (int16_t *)os_malloc(frame_bytes);

    if (pcm == NULL)
    {
        LOGE("ringtone pcm alloc failed\n");
        goto out;
    }

    while (dev->feed_run)
    {
        if ((frame_idx % period_frames) < on_frames)
        {
            uint32_t on_pos = frame_idx % period_frames;
            for (uint32_t i = 0; i < frame_samples; i++)
            {
                float env = 1.0f;
                if (on_pos == 0U)
                {
                    env = (float)i / (float)frame_samples;
                }
                else if (on_pos == (on_frames - 1U))
                {
                    env = 1.0f - (float)i / (float)frame_samples;
                }
                float s = (sinf(phase1) + sinf(phase2)) * env;
                int32_t v = (int32_t)(s * RING_AMPLITUDE * 32767.0f);
                if (v > 32767)
                {
                    v = 32767;
                }
                else if (v < -32768)
                {
                    v = -32768;
                }
                pcm[i] = (int16_t)v;
                phase1 += step1;
                phase2 += step2;
                if (phase1 > two_pi)
                {
                    phase1 -= two_pi;
                }
                if (phase2 > two_pi)
                {
                    phase2 -= two_pi;
                }
            }
        }
        else
        {
            os_memset(pcm, 0, frame_bytes);
        }

        int written = raw_stream_write(dev->raw_stream, (char *)pcm, (int)frame_bytes);
        if (written < 0)
        {
            break;
        }
        frame_idx++;
    }

    os_free(pcm);

out:
    if (dev->feeder_done != NULL)
    {
        rtos_set_semaphore(&dev->feeder_done);
    }
    rtos_delete_thread(NULL);
}

bk_err_t doorbell_ringtone_start(void)
{
    doorbell_ringtone_stop();

    os_memset(&s_ring, 0, sizeof(s_ring));

    const uint32_t frame_bytes = ring_frame_bytes();

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_ring.pipeline = audio_pipeline_init(&pipeline_cfg);
    if (s_ring.pipeline == NULL)
    {
        LOGE("ringtone pipeline init failed\n");
        return BK_FAIL;
    }

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    raw_cfg.output_port_type = PORT_TYPE_RB;
    raw_cfg.out_block_size = frame_bytes;
    raw_cfg.out_block_num = 4;
    s_ring.raw_stream = raw_stream_init(&raw_cfg);
    if (s_ring.raw_stream == NULL)
    {
        LOGE("ringtone raw_stream init failed\n");
        ringtone_pipeline_destroy(&s_ring);
        return BK_FAIL;
    }

    onboard_speaker_stream_cfg_t spk_cfg = ONBOARD_SPEAKER_STREAM_CFG_DEFAULT();
    aud_dac_source_t main_src = AUD_DAC_SOURCE_CALL;
    for (int i = 0; i < AUD_DAC_SOURCE_MAX; i++)
    {
        spk_cfg.sample_rate[i] = RING_SAMPLE_RATE;
        spk_cfg.frame_size[i]  = frame_bytes;
    }
    spk_cfg.chl_num = RING_CHANNELS;
    spk_cfg.bits = RING_BITS;
    spk_cfg.dac_source_bitmap = (1u << main_src);
    spk_cfg.main_dac_source = main_src;
    spk_cfg.dig_gain = 0.0f;
    spk_cfg.ana_gain = 7;
    spk_cfg.multi_in_port_num = 0;
    spk_cfg.multi_out_port_num = 0;
#if CONFIG_BOOT_VIDEO_PA_CTRL_ENABLE
    spk_cfg.pa_ctrl_en   = true;
    spk_cfg.pa_ctrl_gpio = CONFIG_BOOT_VIDEO_PA_CTRL_GPIO;
    spk_cfg.pa_on_level  = CONFIG_BOOT_VIDEO_PA_ON_LEVEL;
    spk_cfg.pa_on_delay  = 2;
    spk_cfg.pa_off_delay = 0;
#endif
    s_ring.speaker = onboard_speaker_stream_init(&spk_cfg);
    if (s_ring.speaker == NULL)
    {
        LOGE("ringtone speaker init failed\n");
        ringtone_pipeline_destroy(&s_ring);
        return BK_FAIL;
    }

    if (audio_pipeline_register(s_ring.pipeline, s_ring.raw_stream, "raw_stream") != BK_OK ||
        audio_pipeline_register(s_ring.pipeline, s_ring.speaker, "onboard_speaker") != BK_OK)
    {
        LOGE("ringtone pipeline register failed\n");
        ringtone_pipeline_destroy(&s_ring);
        return BK_FAIL;
    }

    const char *link_tag[] = {"raw_stream", "onboard_speaker"};
    if (audio_pipeline_link(s_ring.pipeline, link_tag, 2) != BK_OK)
    {
        LOGE("ringtone pipeline link failed\n");
        ringtone_pipeline_destroy(&s_ring);
        return BK_FAIL;
    }

    if (audio_pipeline_run(s_ring.pipeline) != BK_OK)
    {
        LOGE("ringtone pipeline run failed\n");
        ringtone_pipeline_destroy(&s_ring);
        return BK_FAIL;
    }

    if (rtos_init_semaphore(&s_ring.feeder_done, 1) != BK_OK)
    {
        LOGE("ringtone feeder sem init failed\n");
        ringtone_pipeline_destroy(&s_ring);
        return BK_FAIL;
    }

    s_ring.feed_run = true;
    if (rtos_create_thread(&s_ring.feeder, RING_FEEDER_PRIO, "db_ring",
                           (beken_thread_function_t)ringtone_feeder_thread,
                           RING_FEEDER_STACK, &s_ring) != BK_OK)
    {
        LOGE("ringtone feeder create failed\n");
        s_ring.feed_run = false;
        rtos_deinit_semaphore(&s_ring.feeder_done);
        s_ring.feeder_done = NULL;
        ringtone_pipeline_destroy(&s_ring);
        return BK_FAIL;
    }

    s_ring.open = true;
    LOGI("incoming ringtone started\n");
    return BK_OK;
}

void doorbell_ringtone_stop(void)
{
    if (!s_ring.open && s_ring.pipeline == NULL)
    {
        return;
    }

    s_ring.feed_run = false;
    /* Stopping the pipeline unblocks a feeder stuck in raw_stream_write. */
    if (s_ring.pipeline != NULL)
    {
        audio_pipeline_stop(s_ring.pipeline);
        audio_pipeline_wait_for_stop(s_ring.pipeline);
    }

    if (s_ring.feeder != NULL && s_ring.feeder_done != NULL)
    {
        (void)rtos_get_semaphore(&s_ring.feeder_done, 1000);
        s_ring.feeder = NULL;
    }
    if (s_ring.feeder_done != NULL)
    {
        rtos_deinit_semaphore(&s_ring.feeder_done);
        s_ring.feeder_done = NULL;
    }

    ringtone_pipeline_destroy(&s_ring);
    s_ring.open = false;
    LOGI("incoming ringtone stopped\n");
}

#else /* !CONFIG_DOOR_ACCESS_CALL_FLOW */

bk_err_t doorbell_ringtone_start(void)
{
    return BK_OK;
}

void doorbell_ringtone_stop(void)
{
}

#endif /* CONFIG_DOOR_ACCESS_CALL_FLOW */
