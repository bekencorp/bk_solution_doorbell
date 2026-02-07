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
         parameters->rmt_player_sample_rate, parameters->rmt_recoder_fmt, parameters->rmt_player_fmt);

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
        voice_cfg.mic_cfg.onboard_mic_cfg.frame_size = mic_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
        //voice_cfg.mic_cfg.onboard_mic_cfg.out_rb_size = voice_cfg.mic_cfg.onboard_mic_cfg.frame_size;
        voice_cfg.mic_cfg.onboard_mic_cfg.out_block_size = voice_cfg.mic_cfg.onboard_mic_cfg.frame_size;
        voice_cfg.mic_cfg.onboard_mic_cfg.out_block_num = 2;

        voice_cfg.spk_cfg.onboard_spk_cfg.sample_rate = spk_sample_rate;
        voice_cfg.spk_cfg.onboard_spk_cfg.frame_size = spk_sample_rate * 2 * 20 / 1000; //one frame size(20ms)
    }

    if (parameters->aec == 1)
    {
        voice_cfg.aec_en = true;
        voice_cfg.aec_cfg.aec_alg_cfg.aec_cfg.fs = mic_sample_rate;
    }
    else
    {
        voice_cfg.aec_en = false;
    }

    switch (parameters->rmt_recoder_fmt)
    {
        case CODEC_FORMAT_G711A:
        case CODEC_FORMAT_G711U:
        {
            /* g711 encoder config */
            g711_encoder_cfg_t g711_encoder_cfg = DEFAULT_G711_ENCODER_CONFIG();
            voice_cfg.enc_cfg.g711_enc_cfg = g711_encoder_cfg;
            if (parameters->rmt_recoder_fmt == CODEC_FORMAT_G711A)
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
            if (parameters->rmt_recoder_fmt == CODEC_FORMAT_G711A)
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
