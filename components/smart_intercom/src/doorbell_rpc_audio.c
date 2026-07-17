#include <common/bk_include.h>
#include <os/str.h>
#include <os/os.h>

#include "cJSON.h"

#include "doorbell_comm.h"
#include "doorbell_cmd.h"
#include "doorbell_audio_device.h"
#include "doorbell_rpc_internal.h"

#define TAG "db-rpc-aud"
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static cJSON *db_json_int(cJSON *obj, const char *key)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, key) : NULL;
    return (it != NULL && cJSON_IsNumber(it)) ? it : NULL;
}

static cJSON *db_json_str(cJSON *obj, const char *key)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, key) : NULL;
    return (it != NULL && cJSON_IsString(it)) ? it : NULL;
}

/* "pcm"/"g711a"/"g711u" -> codec_format_t; returns BK_FAIL if unsupported. */
static int db_parse_codec_fmt(const char *s, uint8_t *out)
{
    if (os_strcmp(s, "pcm") == 0)      { *out = CODEC_FORMAT_PCM;   return BK_OK; }
    if (os_strcmp(s, "g711a") == 0)    { *out = CODEC_FORMAT_G711A; return BK_OK; }
    if (os_strcmp(s, "g711u") == 0)    { *out = CODEC_FORMAT_G711U; return BK_OK; }
    /* "g722" is defined by the protocol but not by the device codec enum. */
    return BK_FAIL;
}

/* echoDepth/maxAmplitude/minAmplitude/noiseLevel/noiseParam -> audio_acoustics_t. */
static int db_parse_acoustics_name(const char *s, uint32_t *out)
{
    if (os_strcmp(s, "echoDepth") == 0)     { *out = AA_ECHO_DEPTH;    return BK_OK; }
    if (os_strcmp(s, "maxAmplitude") == 0)  { *out = AA_MAX_AMPLITUDE; return BK_OK; }
    if (os_strcmp(s, "minAmplitude") == 0)  { *out = AA_MIN_AMPLITUDE; return BK_OK; }
    if (os_strcmp(s, "noiseLevel") == 0)    { *out = AA_NOISE_LEVEL;   return BK_OK; }
    if (os_strcmp(s, "noiseParam") == 0)    { *out = AA_NOISE_PARAM;   return BK_OK; }
    return BK_FAIL;
}

/* doorbell.audio.turnOn : open two-way voice call. */
bk_err_t doorbell_rpc_audio_turn_on(cJSON *params, cJSON *id)
{
#ifdef CONFIG_VOICE_SERVICE
    audio_parameters_t parameters = {0};
    cJSON *aec = db_json_int(params, "aec");
    cJSON *mic_type = db_json_str(params, "micType");
    cJSON *spk_type = db_json_str(params, "spkType");
    cJSON *rec_sr = db_json_int(params, "recordSampleRate");
    cJSON *play_sr = db_json_int(params, "playSampleRate");
    cJSON *rec_fmt = db_json_str(params, "recordFmt");
    cJSON *play_fmt = db_json_str(params, "playFmt");
    cJSON *asr = db_json_int(params, "asr");
    int ret;

    if (aec == NULL || mic_type == NULL || spk_type == NULL || rec_sr == NULL ||
        play_sr == NULL || rec_fmt == NULL || play_fmt == NULL || asr == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid audio params", NULL);
    }

    if (db_parse_codec_fmt(rec_fmt->valuestring, &parameters.rmt_recorder_fmt) != BK_OK ||
        db_parse_codec_fmt(play_fmt->valuestring, &parameters.rmt_player_fmt) != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Unsupported codec fmt", NULL);
    }

    parameters.aec = (uint8_t)aec->valueint;
    parameters.uac = (os_strcmp(mic_type->valuestring, "uac") == 0 ||
                      os_strcmp(spk_type->valuestring, "uac") == 0) ? 1 : 0;
    parameters.rmt_recorder_sample_rate = (uint32_t)rec_sr->valueint;
    parameters.rmt_player_sample_rate = (uint32_t)play_sr->valueint;
    parameters.asr = (uint8_t)asr->valueint;

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    bool audio_vote_was_set = (doorbell_mm_service_get_status() & MM_STATUS_AUDIO_MASK) != 0;
    doorbell_mm_service_vote(MM_STATUS_AUDIO_BIT, true);
#endif

#if (CONFIG_ASR_SERVICE_WITH_MIC)
    doorbell_asr_turn_off();
#endif

    ret = doorbell_audio_turn_on(&parameters);

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (ret != BK_OK && !audio_vote_was_set)
    {
        doorbell_mm_service_vote(MM_STATUS_AUDIO_BIT, false);
    }
#endif

    if (ret != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "audio turn on failed", NULL);
    }
    return doorbell_rpc_send_result_null(id);
#else
    (void)params;
    return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT, "voice service disabled", NULL);
#endif
}

/* doorbell.audio.turnOff */
bk_err_t doorbell_rpc_audio_turn_off(cJSON *params, cJSON *id)
{
    (void)params;
#ifdef CONFIG_VOICE_SERVICE
    int ret = doorbell_audio_turn_off();

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (ret == BK_OK)
    {
        doorbell_mm_service_vote(MM_STATUS_AUDIO_BIT, false);
    }
#endif

    if (ret != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "audio turn off failed", NULL);
    }
    return doorbell_rpc_send_result_null(id);
#else
    return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT, "voice service disabled", NULL);
#endif
}

/* doorbell.audio.getStatus : result.status = "on" | "off". */
bk_err_t doorbell_rpc_audio_get_status(cJSON *params, cJSON *id)
{
    (void)params;
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    bool on = (doorbell_mm_service_get_status() & MM_STATUS_AUDIO_MASK) != 0;
    return doorbell_rpc_send_status(id, on ? "on" : "off");
#else
    return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "status unavailable", NULL);
#endif
}

/* doorbell.audio.setAcoustics : { name, value }. */
bk_err_t doorbell_rpc_audio_set_acoustics(cJSON *params, cJSON *id)
{
#ifdef CONFIG_VOICE_SERVICE
    cJSON *name = db_json_str(params, "name");
    cJSON *value = db_json_int(params, "value");
    uint32_t index;
    int ret;

    if (name == NULL || value == NULL)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Invalid acoustics params", NULL);
    }

    if (db_parse_acoustics_name(name->valuestring, &index) != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_PARAMS, "Unknown acoustics name", NULL);
    }

    ret = doorbell_audio_acoustics(index, (uint32_t)value->valueint);
    if (ret != BK_OK)
    {
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "setAcoustics failed", NULL);
    }
    return doorbell_rpc_send_result_null(id);
#else
    (void)params;
    return doorbell_rpc_send_error(id, DB_RPC_ERR_NOT_SUPPORT, "voice service disabled", NULL);
#endif
}
