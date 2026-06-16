#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include "doorbell_comm.h"

#include "doorbell_cmd.h"
#include "doorbell_audio_device.h"


#include <components/bk_voice_service.h>
#include <components/bk_voice_service_types.h>
#include <components/bk_voice_read_service.h>
#include <components/bk_voice_read_service_types.h>
#include <components/bk_voice_write_service.h>
#include <components/bk_voice_write_service_types.h>

#if (CONFIG_ASR_SERVICE)
#include <components/bk_audio_asr_service.h>
#include <components/bk_audio_asr_service_types.h>
#include <components/bk_asr_service.h>
#include <components/bk_asr_service_types.h>
#endif

#define TAG "db-aud-dev"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)


extern const doorbell_service_interface_t *doorbell_current_service;

db_audio_device_info_t *gl_db_audio_device_info = NULL;


int doorbell_voice_send_callback(unsigned char *data, unsigned int len, void *args)
{
    audio_enc_type_t enc_type = 0;

    if (args != NULL)
    {
        enc_type = *(audio_enc_type_t *)args;
    }

    return ntwk_trans_audio_send(data, len, enc_type);
}

#if (CONFIG_ASR_SERVICE)
#if CONFIG_BEKEN_KWS
#include "bk_kws_asr.h"
#endif
static float g_audio_engine_asr_score      = 0.0f;
static const char *g_audio_engine_asr_text = NULL;

static uint8_t g_recog_en = 1;

void bk_audio_engine_asr_result_handle(void *p1, void *p2)
{
    if (!g_recog_en)
        return;

    const char *result = NULL;
    uint8_t asr_result = 0;

    /* BK7259 ASR callback passes result and score via p1/p2 pointers. */
    if (p1 != NULL) {
        result = *((char **)p1);
    }
    if (result == NULL) {
        LOGE("ASR result is NULL\n");
        return;
    }
    (void)p2;

#if CONFIG_BEKEN_KWS
    //LOGD("result : %s\n", result);
    if (os_strcmp(result, "nihaobotong") == 0)
    {
        LOGI("nihaobotong\r\n");
        asr_result = 1;
    } else if ((os_strcmp(result, "zaijianbotong") == 0))
    {
        LOGI("%s \n", "zaijianbotong");
        asr_result = 2;
    } else if (os_strcmp(result, "Play Music") == 0)
    {
        LOGI("play music\r\n");
        asr_result = 3;
    } else if (os_strcmp(result, "Stop Play") == 0)
    {
        LOGI("stop play\r\n");
        asr_result = 4;
    }else if (os_strcmp(result, "Next song") == 0)
    {
        LOGI("next song\r\n");
        asr_result = 5;
    } else if (os_strcmp(result, "Volume Up") == 0)
    {
        LOGI("volume up\r\n");
        asr_result = 6;
    } else if (os_strcmp(result, "Volume Down") == 0)
    {
        LOGI("volume down\r\n");
        asr_result = 7;
    }
#endif

#if CONFIG_BEKEN_KWS
    if ((asr_result < 0) || (asr_result > 7)) {
        LOGE("Invalid asr_result: %d, valid range is 0-7\n", asr_result);
        return;
    }
#else
    if ((asr_result == 0) || (asr_result > 2)) {
        LOGE("Invalid asr_result: %d, valid range is 1-2\n", asr_result);
        return;
    }
#endif
}

#if CONFIG_BEKEN_KWS
static int bk_doorbell_asr_recog(void *read_buf, uint32_t read_size, void *p1, void *p2)
{
    int16_t result = 0;

    if (g_recog_en) {
        result = bk_tflite_asr_recog((short *)read_buf, read_size, p1, p2);
    } else {
        result = 1;
    }

    return result;
}
#endif

#endif


#if (CONFIG_ASR_SERVICE_WITH_MIC)
audio_parameters_t g_aud_asr_parameters = {0};

void doorbell_asr_params_init(void)
{
    os_memset(&g_aud_asr_parameters, 0, sizeof(audio_parameters_t));
    g_aud_asr_parameters.aec = 1;
    g_aud_asr_parameters.uac = 0;
    g_aud_asr_parameters.rmt_recorder_sample_rate = DB_SAMPLE_RARE_16K;
    g_aud_asr_parameters.rmt_recorder_fmt = CODEC_FORMAT_PCM;
    g_aud_asr_parameters.asr = 1;
}

int doorbell_asr_turn_on(void)
{
    if (gl_db_audio_device_info == NULL) {
        LOGE("%s, invalid param!", __func__);
        return BK_FAIL;
    }

    if (gl_db_audio_device_info->asr_enable == BK_TRUE)
    {
        LOGD("%s already turn on\n", __func__);
        return BK_FAIL;
    }
#if (CONFIG_VOICE_SERVICE)
	if (gl_db_audio_device_info->audio_enable == BK_TRUE)
#endif
    {
        LOGD("%s, video/audio module open.", __func__);
        return BK_FAIL;
    }

    LOGD("%s entry\n", __func__);
    audio_parameters_t *parameters = &g_aud_asr_parameters;
    uint32_t mic_sample_rate = 8000;
    switch (parameters->rmt_recorder_sample_rate)
    {
        case DB_SAMPLE_RARE_8K:
            mic_sample_rate = 8000;
            break;

        case DB_SAMPLE_RARE_16K:
            mic_sample_rate = 16000;
            break;

        default:
            mic_sample_rate = 8000;
            break;
    }

    parameters->asr = 1;
    asr_cfg_t asr_cfg = {0};

    if (parameters->uac == 1)
    {
        asr_cfg_t asr_uac_cfg = ASR_BY_UAC_MIC_CFG_DEFAULT();
        asr_cfg = asr_uac_cfg;
        asr_cfg.mic_cfg.uac_mic_cfg.samp_rate      = mic_sample_rate;
        asr_cfg.mic_cfg.uac_mic_cfg.frame_size     = mic_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
        asr_cfg.mic_cfg.uac_mic_cfg.out_block_size = asr_cfg.mic_cfg.uac_mic_cfg.frame_size;
        asr_cfg.mic_cfg.uac_mic_cfg.out_block_num  = 4;
    }
    else
    {
        asr_cfg_t asr_onboard_cfg = ASR_BY_ONBOARD_MIC_CFG_DEFAULT();
        asr_cfg = asr_onboard_cfg;
        asr_cfg.mic_cfg.onboard_mic_cfg.adc_cfg.sample_rate = mic_sample_rate;
        asr_cfg.mic_cfg.onboard_mic_cfg.frame_size          = mic_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
        asr_cfg.mic_cfg.onboard_mic_cfg.out_block_size      = asr_cfg.mic_cfg.onboard_mic_cfg.frame_size;
        asr_cfg.mic_cfg.onboard_mic_cfg.out_block_num       = 4;

#if CONFIG_ADK_ONBOARD_MIC_STREAM_V2
        asr_cfg.mic_cfg.onboard_mic_cfg.ch_bitmap       = (1 << AUD_ADC_CHL_0);
        asr_cfg.mic_cfg.onboard_mic_cfg.adc_cfg.chl_num = 0;
        for(uint32_t j = 0; j < AUD_ADC_CHL_MAX; j++)
        {
            if(asr_cfg.mic_cfg.onboard_mic_cfg.ch_bitmap & (1 << j))
            {
                asr_cfg.mic_cfg.onboard_mic_cfg.adc_cfg.chl_num++;
            }
        }
        asr_cfg.mic_cfg.onboard_mic_cfg.adc_cfg.aec_en = parameters->aec;
#endif
    }

    if (parameters->asr == 1)
    {
        asr_cfg.asr_en = true;
        if (mic_sample_rate != asr_cfg.asr_sample_rate)
        {
#if CONFIG_ADK_RSP_ALGORITHM
            asr_cfg.asr_rsp_en = true;
            asr_cfg.rsp_cfg.rsp_alg_cfg.rsp_cfg.src_rate = mic_sample_rate;
#else
            asr_cfg.asr_rsp_en = false;
            LOGE("Need Open the aud resample Macro\n");
            goto error;
#endif
        } else
        {
            asr_cfg.asr_rsp_en = false;
        }
    }
    else
    {
        asr_cfg.asr_en     = false;
        asr_cfg.asr_rsp_en = false;
    }

    if (parameters->aec && !parameters->uac)
    {
        asr_cfg.aec_en = true;
        asr_cfg.aec_cfg.aec_alg_cfg.aec_cfg.mode                    = AEC_MODE_HARDWARE;
        asr_cfg.aec_cfg.aec_alg_cfg.aec_cfg.ns_type                 = NS_TRADITION;
        asr_cfg.aec_cfg.aec_alg_cfg.dual_ch                         = 0;
        asr_cfg.aec_cfg.aec_alg_cfg.multi_in_port_num               = 0;
        asr_cfg.aec_cfg.aec_alg_cfg.vad_cfg.vad_enable              = 0;
        asr_cfg.aec_cfg.aec_alg_cfg.aec_cfg.ec_only_output          = 1;
        asr_cfg.aec_cfg.aec_alg_cfg.aec_cfg.multi_output_use_ec_out = 1;
        asr_cfg.aec_cfg.aec_alg_cfg.out_block_num = 4;
    } else
    {
        asr_cfg.aec_en = false;
    }

    if (asr_cfg.asr_en == true)
    {
        asr_cfg.event_handle = NULL;
        asr_cfg.args         = NULL;
        gl_db_audio_device_info->asr_handle = bk_asr_create(&asr_cfg);
        if (!gl_db_audio_device_info->asr_handle)
        {
            LOGE("asr init fail\n");
            goto error;
        }

        if (mic_sample_rate == 16000) {
            asr_cfg.read_pool_size = mic_sample_rate * 2 * 20 / 1000;
        }
        else if (mic_sample_rate == 8000) {
            asr_cfg.read_pool_size = 2 * mic_sample_rate * 2 * 20 / 1000;
        }

        bk_asr_init_with_mic(&asr_cfg, gl_db_audio_device_info->asr_handle);

        {
            aud_asr_cfg_t aud_asr_cfg = AUDIO_ASR_CFG_DEFAULT();
            aud_asr_cfg.asr_handle    = gl_db_audio_device_info->asr_handle;

            aud_asr_cfg.aud_asr_result_handle = bk_audio_engine_asr_result_handle;
        #if CONFIG_BEKEN_KWS
            aud_asr_cfg.aud_asr_init   = bk_tflite_asr_init;
            aud_asr_cfg.aud_asr_deinit = bk_tflite_asr_deinit;
            aud_asr_cfg.aud_asr_recog  = bk_doorbell_asr_recog;
            aud_asr_cfg.max_read_size  = 1280;
            aud_asr_cfg.task_stack     = 25 * 1024;
            aud_asr_cfg.mem_type       = AUDIO_MEM_TYPE_PSRAM;
        #endif
            aud_asr_cfg.p1             = (void *)&g_audio_engine_asr_text;
            aud_asr_cfg.p2             = (void *)&g_audio_engine_asr_score;

            gl_db_audio_device_info->aud_asr_handle = bk_aud_asr_init(&aud_asr_cfg);
            if (!gl_db_audio_device_info->aud_asr_handle)
            {
                LOGE("aud asr init fail\n");
                goto error;
            }
        }
    }

    if (asr_cfg.asr_en == true)
    {
        if (BK_OK != bk_asr_start(gl_db_audio_device_info->asr_handle))
        {
            LOGE("asr start fail\n");
            goto error;
        }
        if (BK_OK != bk_aud_asr_start(gl_db_audio_device_info->aud_asr_handle))
        {
            LOGE("aud asr start fail\n");
            goto error;
        }
    }
    gl_db_audio_device_info->asr_enable = BK_TRUE;
    LOGD("%s out\n", __func__);

    return BK_OK;
error:
    if (gl_db_audio_device_info->aud_asr_handle)
    {
        bk_aud_asr_stop(gl_db_audio_device_info->aud_asr_handle);
    }
    if (gl_db_audio_device_info->asr_handle)
    {
        bk_asr_stop(gl_db_audio_device_info->asr_handle);
    }
    if (gl_db_audio_device_info->aud_asr_handle)
    {
        bk_aud_asr_deinit(gl_db_audio_device_info->aud_asr_handle);
    }

    gl_db_audio_device_info->aud_asr_handle = NULL;
    gl_db_audio_device_info->asr_handle     = NULL;
    gl_db_audio_device_info->asr_enable     = BK_FALSE;
    return BK_FAIL;
}

int doorbell_asr_turn_off(void)
{
    if (gl_db_audio_device_info == NULL) {
        LOGE("%s, invalid param!", __func__);
        return BK_FAIL;
    }

    if (gl_db_audio_device_info->asr_enable == BK_FALSE)
    {
        LOGD("%s already turn off\n", __func__);
        return BK_FAIL;
    }
    LOGD("%s entry\n", __func__);

    gl_db_audio_device_info->asr_enable = BK_FALSE;

    if (gl_db_audio_device_info->aud_asr_handle)
    {
        bk_aud_asr_stop(gl_db_audio_device_info->aud_asr_handle);
    }
    if (gl_db_audio_device_info->asr_handle)
    {
        bk_asr_stop(gl_db_audio_device_info->asr_handle);
    }

    if (gl_db_audio_device_info->aud_asr_handle)
    {
        bk_aud_asr_deinit(gl_db_audio_device_info->aud_asr_handle);
    }
    if (gl_db_audio_device_info->asr_handle)
    {
        bk_asr_deinit(gl_db_audio_device_info->asr_handle);
    }

    gl_db_audio_device_info->aud_asr_handle = NULL;
    gl_db_audio_device_info->asr_handle     = NULL;
    gl_db_audio_device_info->asr_enable     = BK_FALSE;

    LOGD("%s out\n", __func__);
    return BK_OK;
}

void doorbell_asr_arbitrate(void)
{
    bool busy;

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    /* Resource-constrained: any active media service (camera/audio/lcd) preempts ASR. */
    busy = (doorbell_mm_service_get_status() != 0);
#else
    busy = (gl_db_audio_device_info != NULL &&
            gl_db_audio_device_info->audio_enable == BK_TRUE);
#endif

    /* ASR is armed only once at boot/wakeup (see ap_main). At runtime we only ever
     * turn it OFF when a media service is active; the idle gap before keepalive
     * sleep is too short to be worth re-arming, so we never turn it back on here. */
    if (busy)
    {
        doorbell_asr_turn_off();
    }
}

void cli_doorbell_asr_turn_off(void)
{
    doorbell_asr_turn_off();
}

void cli_doorbell_asr_turn_on(uint32_t aec, uint32_t uac, uint32_t sample_rate, uint8_t asr_en)
{
    g_aud_asr_parameters.aec = aec;
    g_aud_asr_parameters.uac = uac;
    g_aud_asr_parameters.rmt_recorder_sample_rate = sample_rate;
    g_recog_en = asr_en;

    doorbell_asr_turn_on();
}

#endif


int doorbell_audio_turn_off(void)
{
    if (gl_db_audio_device_info->audio_enable == BK_FALSE)
    {
        LOGD("%s already turn off\n", __func__);

        return BK_FAIL;
    }

    LOGD("%s entry\n", __func__);

    gl_db_audio_device_info->audio_enable = BK_FALSE;

    #if 0
    if (doorbell_current_service
        && doorbell_current_service->audio_state_changed)
    {
        doorbell_current_service->audio_state_changed(DB_TURN_OFF);
    }
    #endif
    const char *service_name = ntwk_trans_get_service_name();

    if (service_name == NULL)
    {
        LOGE("%s, service_name is NULL\n", __func__);
        return BK_FAIL;
    }
    if (strcmp(service_name, "cs2_service") == 0)
    {
        ntwk_trans_chan_stop(NTWK_TRANS_CHAN_AUDIO);
    }

    if (gl_db_audio_device_info->voice_read_handle)
    {
        bk_voice_read_stop(gl_db_audio_device_info->voice_read_handle);
    }

    if (gl_db_audio_device_info->voice_write_handle)
    {
        bk_voice_write_stop(gl_db_audio_device_info->voice_write_handle);
    }

    if (gl_db_audio_device_info->voice_handle)
    {
        bk_voice_stop(gl_db_audio_device_info->voice_handle);
    }

    if (gl_db_audio_device_info->voice_read_handle)
    {
        bk_voice_read_deinit(gl_db_audio_device_info->voice_read_handle);
    }

    if (gl_db_audio_device_info->voice_write_handle)
    {
        bk_voice_write_deinit(gl_db_audio_device_info->voice_write_handle);
    }

    if (gl_db_audio_device_info->voice_handle)
    {
        bk_voice_deinit(gl_db_audio_device_info->voice_handle);
    }
    gl_db_audio_device_info->voice_read_handle = NULL;
    gl_db_audio_device_info->voice_write_handle = NULL;
    gl_db_audio_device_info->voice_handle  = NULL;

    LOGD("%s out\n", __func__);
    return BK_OK;
}

bk_err_t doorbell_audio_event_handle(voice_evt_t event, void *param, void *args)
{
    doorbell_msg_t msg;

    switch (event)
    {
        case VOC_EVT_MIC_NOT_SUPPORT:
        case VOC_EVT_SPK_NOT_SUPPORT:
        case VOC_EVT_ERROR_UNKNOW:
        case VOC_EVT_STOP:
            LOGD("%s, -->>event: %d\n", __func__, event);
            msg.event = DBEVT_VOICE_EVENT;
            msg.param = event;
            doorbell_send_msg(&msg);
            break;

        default:
            break;
    }

    return BK_OK;
}

int doorbell_audio_turn_on(audio_parameters_t *parameters)
{
    voice_cfg_t voice_cfg = {0};

    if (gl_db_audio_device_info->audio_enable == BK_TRUE)
    {
        LOGD("%s already turn on\n", __func__);

        return BK_FAIL;
    }

    LOGD("%s, AEC: %d, UAC: %d, sample rate: %d, %d, fmt: %d, %d\n", __func__,
         parameters->aec, parameters->uac, parameters->rmt_recorder_sample_rate,
         parameters->rmt_player_sample_rate, parameters->rmt_recorder_fmt, parameters->rmt_player_fmt);

    uint32_t mic_sample_rate = 8000;
    uint32_t spk_sample_rate = 8000;
    switch (parameters->rmt_recorder_sample_rate)
    {
        case DB_SAMPLE_RARE_8K:
            mic_sample_rate = 8000;
            break;

        case DB_SAMPLE_RARE_16K:
            mic_sample_rate = 16000;
            break;

        default:
            mic_sample_rate = 8000;
            break;
    }

    switch (parameters->rmt_player_sample_rate)
    {
        case DB_SAMPLE_RARE_8K:
            spk_sample_rate = 8000;
            break;

        case DB_SAMPLE_RARE_16K:
            spk_sample_rate = 16000;
            break;

        default:
            spk_sample_rate = 8000;
            break;
    }


    if (parameters->uac == 1)
    {
        voice_cfg_t voice_uac_cfg = VOICE_BY_UAC_MIC_SPK_CFG_DEFAULT();
        voice_cfg = voice_uac_cfg;
        voice_cfg.mic_cfg.uac_mic_cfg.samp_rate = mic_sample_rate;
        voice_cfg.mic_cfg.uac_mic_cfg.frame_size = mic_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
        voice_cfg.mic_cfg.uac_mic_cfg.out_block_size = voice_cfg.mic_cfg.uac_mic_cfg.frame_size;
        voice_cfg.mic_cfg.uac_mic_cfg.out_block_num = 2;

        voice_cfg.spk_cfg.uac_spk_cfg.samp_rate = spk_sample_rate;
        voice_cfg.spk_cfg.uac_spk_cfg.frame_size = spk_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
    }
    else
    {
        voice_cfg_t voice_onboard_cfg = VOICE_BY_ONBOARD_MIC_SPK_CFG_DEFAULT();
        voice_cfg = voice_onboard_cfg;
        voice_cfg.mic_cfg.onboard_mic_cfg.adc_cfg.sample_rate = mic_sample_rate;
        voice_cfg.mic_cfg.onboard_mic_cfg.frame_size          = mic_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
        //voice_cfg.mic_cfg.onboard_mic_cfg.out_rb_size       = voice_cfg.mic_cfg.onboard_mic_cfg.frame_size;
        voice_cfg.mic_cfg.onboard_mic_cfg.out_block_size      = voice_cfg.mic_cfg.onboard_mic_cfg.frame_size;
        voice_cfg.mic_cfg.onboard_mic_cfg.out_block_num       = 2;

        #if CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2
        if (spk_sample_rate == 8000)
        {
            voice_cfg.spk_cfg.onboard_spk_cfg.dac_source_bitmap = ONBOARD_SPEAKER_STREAM_DAC_SOURCE_CALL_BIT;
            voice_cfg.spk_cfg.onboard_spk_cfg.main_dac_source   = AUD_DAC_SOURCE_CALL;
            voice_cfg.spk_cfg.onboard_spk_cfg.sample_rate[AUD_DAC_SOURCE_CALL] = spk_sample_rate;
            voice_cfg.spk_cfg.onboard_spk_cfg.frame_size[AUD_DAC_SOURCE_CALL]  = spk_sample_rate * 2 * 20 / 1000; //one frame size(20ms)

        } else
        {
            voice_cfg.spk_cfg.onboard_spk_cfg.dac_source_bitmap = ONBOARD_SPEAKER_STREAM_DAC_SOURCE_A2DP_BIT;
            voice_cfg.spk_cfg.onboard_spk_cfg.main_dac_source   = AUD_DAC_SOURCE_A2DP;
            voice_cfg.spk_cfg.onboard_spk_cfg.sample_rate[AUD_DAC_SOURCE_A2DP] = spk_sample_rate;
            voice_cfg.spk_cfg.onboard_spk_cfg.frame_size[AUD_DAC_SOURCE_A2DP]  = spk_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
        }
        #else
        voice_cfg.spk_cfg.onboard_spk_cfg.sample_rate = spk_sample_rate;
        voice_cfg.spk_cfg.onboard_spk_cfg.frame_size  = spk_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
        #endif
        #if CONFIG_ADK_ONBOARD_MIC_STREAM_V2
        voice_cfg.mic_cfg.onboard_mic_cfg.ch_bitmap       = (1 << AUD_ADC_CHL_0);
        voice_cfg.mic_cfg.onboard_mic_cfg.adc_cfg.chl_num = 0;
        for(uint32_t j = 0; j < AUD_ADC_CHL_MAX; j++)
        {
            if(voice_cfg.mic_cfg.onboard_mic_cfg.ch_bitmap & (1 << j))
            {
                voice_cfg.mic_cfg.onboard_mic_cfg.adc_cfg.chl_num++;
            }
        }
        voice_cfg.mic_cfg.onboard_mic_cfg.adc_cfg.aec_en = parameters->aec;
        #endif
    }

    if (parameters->aec == 1)
    {
        voice_cfg.aec_en = true;
        voice_cfg.aec_cfg.aec_alg_cfg.aec_cfg.fs = mic_sample_rate;

        if (parameters->uac == 1)
        {
            voice_cfg.aec_cfg.aec_alg_cfg.aec_cfg.mode      = AEC_MODE_SOFTWARE;
            voice_cfg.aec_cfg.aec_alg_cfg.dual_ch           = 0;
            voice_cfg.aec_cfg.aec_alg_cfg.multi_in_port_num = 1;
        } else
        {
            #if CONFIG_SOC_BK7259
            voice_cfg.aec_cfg.aec_alg_cfg.dual_ch           = 0;
            voice_cfg.aec_cfg.aec_alg_cfg.aec_cfg.mode      = AEC_MODE_HARDWARE;
            voice_cfg.aec_cfg.aec_alg_cfg.multi_in_port_num = 0;
            #endif
        }
    }
    else
    {
        voice_cfg.aec_en = false;
    }

    switch (parameters->rmt_recorder_fmt)
    {
        case CODEC_FORMAT_G711A:
        case CODEC_FORMAT_G711U:
        {
            /* g711 encoder config */
            g711_encoder_cfg_t g711_encoder_cfg = DEFAULT_G711_ENCODER_CONFIG();
            voice_cfg.enc_cfg.g711_enc_cfg = g711_encoder_cfg;
            if (parameters->rmt_recorder_fmt == CODEC_FORMAT_G711A)
            {
                voice_cfg.enc_type = AUDIO_ENC_TYPE_G711A;
                voice_cfg.enc_cfg.g711_enc_cfg.enc_mode = G711_ENC_MODE_A_LOW;
            }
            else
            {
                voice_cfg.enc_type = AUDIO_ENC_TYPE_G711U;
                voice_cfg.enc_cfg.g711_enc_cfg.enc_mode = G711_ENC_MODE_U_LOW;
            }
            voice_cfg.enc_cfg.g711_enc_cfg.buf_sz = mic_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
            voice_cfg.enc_cfg.g711_enc_cfg.out_block_size = voice_cfg.enc_cfg.g711_enc_cfg.buf_sz >> 1;
            /* config raw_read input buffer */
            voice_cfg.read_pool_size = voice_cfg.enc_cfg.g711_enc_cfg.out_block_size;

            /* g711 decoder config */
            g711_decoder_cfg_t g711_decoder_cfg = DEFAULT_G711_DECODER_CONFIG();
            voice_cfg.dec_cfg.g711_dec_cfg = g711_decoder_cfg;
            if (parameters->rmt_recorder_fmt == CODEC_FORMAT_G711A)
            {
                voice_cfg.dec_type = AUDIO_DEC_TYPE_G711A;
                voice_cfg.dec_cfg.g711_dec_cfg.dec_mode = G711_DEC_MODE_A_LOW;
            }
            else
            {
                voice_cfg.dec_type = AUDIO_DEC_TYPE_G711U;
                voice_cfg.dec_cfg.g711_dec_cfg.dec_mode = G711_DEC_MODE_U_LOW;
            }
            voice_cfg.dec_cfg.g711_dec_cfg.out_block_size = spk_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
            voice_cfg.dec_cfg.g711_dec_cfg.buf_sz = voice_cfg.dec_cfg.g711_dec_cfg.out_block_size >> 1;
            /* config raw_write output buffer */
            voice_cfg.write_pool_size = voice_cfg.dec_cfg.g711_dec_cfg.buf_sz;
        }
        break;

        case CODEC_FORMAT_PCM:
        {
            /* pcm encoder config */
            voice_cfg.enc_type = AUDIO_ENC_TYPE_PCM;
            voice_cfg.enc_cfg.pcm_enc_cfg = 0;      // not used
            voice_cfg.dec_type = AUDIO_DEC_TYPE_PCM;
            voice_cfg.dec_cfg.pcm_dec_cfg = 0;      //not used

            /* config raw_read input buffer and raw_write output buffer */
            voice_cfg.read_pool_size = mic_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
            voice_cfg.write_pool_size = spk_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
        }
        break;

        default:
        {
            LOGE("not support encoder format\n");
            goto error;
        }
        break;
    }

    //voice_cfg.event_handle = doorbell_audio_event_handle; /* close audio event, because sram is not enough */
    voice_cfg.event_handle = NULL;
    voice_cfg.args = NULL;
    gl_db_audio_device_info->voice_handle = bk_voice_init(&voice_cfg);
    if (!gl_db_audio_device_info->voice_handle)
    {
        LOGE("voice init fail\n");
        goto error;
    }

    voice_read_cfg_t voice_read_cfg = VOICE_READ_CFG_DEFAULT();
    voice_read_cfg.voice_handle = gl_db_audio_device_info->voice_handle;
    //voice_read_cfg.max_read_size = mic_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
    voice_read_cfg.max_read_size = 1280;//mic_sample_rate * 2 * 20 * 10 / 1000; //one frame size(200ms)
    voice_read_cfg.voice_read_callback = doorbell_voice_send_callback;
    voice_read_cfg.args = NULL;
    voice_read_cfg.task_stack = 1024 * 4;
    voice_read_cfg.mem_type = AUDIO_MEM_TYPE_PSRAM;
    gl_db_audio_device_info->voice_read_handle = bk_voice_read_init(&voice_read_cfg);
    if (!gl_db_audio_device_info->voice_read_handle)
    {
        LOGE("voice read init fail\n");
        goto error;
    }

    voice_write_cfg_t voice_write_cfg = VOICE_WRITE_CFG_DEFAULT();
    voice_write_cfg.voice_handle = gl_db_audio_device_info->voice_handle;
    voice_write_cfg.mem_type = AUDIO_MEM_TYPE_PSRAM;
    gl_db_audio_device_info->voice_write_handle = bk_voice_write_init(&voice_write_cfg);
    if (!gl_db_audio_device_info->voice_write_handle)
    {
        LOGE("voice write init fail\n");
        goto error;
    }

    if (BK_OK != bk_voice_start(gl_db_audio_device_info->voice_handle))
    {
        LOGE("voice start fail\n");
        goto error;
    }

    if (BK_OK != bk_voice_read_start(gl_db_audio_device_info->voice_read_handle))
    {
        LOGE("voice read start fail\n");
        goto error;
    }

    if (BK_OK != bk_voice_write_start(gl_db_audio_device_info->voice_write_handle))
    {
        LOGE("voice write start fail\n");
        goto error;
    }

    gl_db_audio_device_info->audio_enable = BK_TRUE;

    #if 0
    if (doorbell_current_service
        && doorbell_current_service->audio_state_changed)
    {
        doorbell_current_service->audio_state_changed(DB_TURN_ON);
    }
    #endif

    const char *service_name = ntwk_trans_get_service_name();

    if (service_name == NULL)
    {
        LOGE("%s, service_name is NULL\n", __func__);
        return BK_FAIL;
    }
    if (strcmp(service_name, "cs2_service") == 0)
    {
        ntwk_trans_chan_start(NTWK_TRANS_CHAN_AUDIO, NULL);
    }

    return BK_OK;
error:
    if (gl_db_audio_device_info->voice_read_handle)
    {
        bk_voice_read_stop(gl_db_audio_device_info->voice_read_handle);
    }

    if (gl_db_audio_device_info->voice_write_handle)
    {
        bk_voice_write_stop(gl_db_audio_device_info->voice_write_handle);
    }

    if (gl_db_audio_device_info->voice_handle)
    {
        bk_voice_stop(gl_db_audio_device_info->voice_handle);
    }

    if (gl_db_audio_device_info->voice_read_handle)
    {
        bk_voice_read_deinit(gl_db_audio_device_info->voice_read_handle);
    }

    if (gl_db_audio_device_info->voice_write_handle)
    {
        bk_voice_write_deinit(gl_db_audio_device_info->voice_write_handle);
    }

    if (gl_db_audio_device_info->voice_handle)
    {
        bk_voice_deinit(gl_db_audio_device_info->voice_handle);
    }
    gl_db_audio_device_info->voice_read_handle = NULL;
    gl_db_audio_device_info->voice_write_handle  = NULL;
    gl_db_audio_device_info->voice_handle  = NULL;

    return BK_FAIL;
}

void cli_doorbell_audio_turn_on(uint32_t aec, uint32_t uac, uint32_t sample_rate, uint32_t fmt)
{
    LOGD("%s, aec: %u, uac: %u, sample_rate: %u, fmt: %u\n", __func__, aec, uac, sample_rate, fmt);
    audio_parameters_t parameters = {0};
    parameters.aec = aec;
    parameters.uac = uac;
    switch (sample_rate)
    {
        case 8000:
            parameters.rmt_recorder_sample_rate = DB_SAMPLE_RARE_8K;
            parameters.rmt_player_sample_rate   = DB_SAMPLE_RARE_8K;
            break;
        case 16000:
            parameters.rmt_recorder_sample_rate = DB_SAMPLE_RARE_16K;
            parameters.rmt_player_sample_rate   = DB_SAMPLE_RARE_16K;
            break;
        default:
            parameters.rmt_recorder_sample_rate = DB_SAMPLE_RARE_16K;
            parameters.rmt_player_sample_rate   = DB_SAMPLE_RARE_16K;
            break;
    }
    switch (fmt)
    {
        case 1:
        case 3:
            parameters.rmt_recorder_fmt = CODEC_FORMAT_G711A;
            parameters.rmt_player_fmt   = CODEC_FORMAT_G711A;
            break;
        case 2:
            parameters.rmt_recorder_fmt = CODEC_FORMAT_PCM;
            parameters.rmt_player_fmt   = CODEC_FORMAT_PCM;
            break;
        default:
            parameters.rmt_recorder_fmt = CODEC_FORMAT_G711A;
            parameters.rmt_player_fmt   = CODEC_FORMAT_G711A;
            break;
    }

    doorbell_audio_turn_on(&parameters);
}

void cli_doorbell_audio_turn_off(void)
{
    LOGD("%s, entry\n", __func__);
    doorbell_audio_turn_off();
}

int doorbell_audio_acoustics(uint32_t index, uint32_t param)
{
    LOGD("%s, %u, %u\n", __func__, index, param);
#if 0
    bk_err_t ret = BK_FAIL;

    switch (index)
    {
        case AA_ECHO_DEPTH:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_EC_DEPTH, param);
            break;
        case AA_MAX_AMPLITUDE:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_TXRX_THR, param);
            break;
        case AA_MIN_AMPLITUDE:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_TXRX_FLR, param);
            break;
        case AA_NOISE_LEVEL:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_NS_LEVEL, param);
            break;
        case AA_NOISE_PARAM:
            ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_NS_PARA, param);
            break;
    }

    return ret;
#endif
    return -1;
}

void doorbell_audio_data_callback(uint8_t *data, uint32_t length)
{
    bk_err_t ret = BK_OK;

    if (gl_db_audio_device_info->audio_enable)
    {
        ret = bk_voice_write_frame_data(gl_db_audio_device_info->voice_write_handle, (char *)data, length);
        if (ret != length)
        {
            LOGV("write speaker data fail, need_write: %d, ret: %d\n", length, ret);
        }
    }
}

int doorbell_audio_device_init(void)
{
    if (gl_db_audio_device_info == NULL)
    {
        gl_db_audio_device_info = os_malloc(sizeof(db_audio_device_info_t));
    }

    if (gl_db_audio_device_info == NULL)
    {
        LOGE("malloc gl_db_audio_device_info failed\n");
        return  BK_FAIL;
    }

    os_memset(gl_db_audio_device_info, 0, sizeof(db_audio_device_info_t));

#if (CONFIG_ASR_SERVICE)
    doorbell_asr_params_init();
#endif

    return BK_OK;
}

void doorbell_audio_device_deinit(void)
{
    if (gl_db_audio_device_info)
    {
        os_free(gl_db_audio_device_info);
        gl_db_audio_device_info = NULL;
    }
}
