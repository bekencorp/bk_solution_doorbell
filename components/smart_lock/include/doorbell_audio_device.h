#ifndef __DOORBELL_AUDIO_DEVICE_H__
#define __DOORBELL_AUDIO_DEVICE_H__

#include <components/bk_voice_service_types.h>
#include <components/bk_voice_read_service_types.h>
#include <components/bk_voice_write_service_types.h>

#include "network_transfer.h"

#if (CONFIG_ASR_SERVICE)
#include <components/bk_asr_service_types.h>
#endif


#define DB_SAMPLE_RARE_8K (8000)
#define DB_SAMPLE_RARE_16K (16000)

/**
 * @brief      Codec format enum
 */
typedef enum
{
    CODEC_FORMAT_UNKNOW = 0,
    CODEC_FORMAT_G711A = 1,
    CODEC_FORMAT_PCM = 2,
    CODEC_FORMAT_G711U = 3,
} codec_format_t;

/**
 * @brief      Audio parameters struct
 */
typedef struct
{
    uint8_t aec;
    uint8_t uac;
    uint8_t rmt_recoder_fmt; /* codec_format_t */
    uint8_t rmt_player_fmt; /* codec_format_t */
    uint32_t rmt_recorder_sample_rate;
    uint32_t rmt_player_sample_rate;
    uint8_t asr;
} audio_parameters_t;

/**
 * @brief      Audio acoustics enum
 */
typedef enum
{
    AA_UNKNOWN = 0,
    AA_ECHO_DEPTH = 1,
    AA_MAX_AMPLITUDE = 2,
    AA_MIN_AMPLITUDE = 3,
    AA_NOISE_LEVEL = 4,
    AA_NOISE_PARAM = 5,
} audio_acoustics_t;

/**
 * @brief      Audio device info struct
 */
typedef struct
{
    uint8_t audio_enable;
    voice_handle_t voice_handle;
    voice_read_handle_t voice_read_handle;
    voice_write_handle_t voice_write_handle;
#if (CONFIG_ASR_SERVICE)
    asr_handle_t asr_handle;
    aud_asr_handle_t aud_asr_handle;
#endif
} db_audio_device_info_t;


/**
 * @brief      Turn on audio device
 *
 * @param[in]      parameters  Audio parameters
 *
 * @return         Result
 *                 - BK_OK: success
 *                 - other: failed
 */
int doorbell_audio_turn_on(audio_parameters_t *parameters);

/**
 * @brief      Turn off audio device
 *
 * @return         Result
 *                 - BK_OK: success
 *                 - other: failed
 */
int doorbell_audio_turn_off(void);

/**
 * @brief      Set audio acoustics
 *
 * @param[in]      index  Audio acoustics index
 * @param[in]      param  Audio acoustics parameter
 *
 * @return         Result
 *                 - BK_OK: success
 *                 - other: failed
 */
int doorbell_audio_acoustics(uint32_t index, uint32_t param);

/**
 * @brief      Audio data callback function
 *             This function is called when audio data is received
 *
 * @param[in]      data    Audio data pointer
 * @param[in]      length  Audio data length
 *
 * @return         Result
 *                 - BK_OK: success
 *                 - other: failed
 */
void doorbell_audio_data_callback(uint8_t *data, uint32_t length);

/**
 * @brief      Audio device init function
 *
 * @return         Result
 *                 - BK_OK: success
 *                 - other: failed
 */
int doorbell_audio_device_init(void);

/**
 * @brief      Audio device deinit function
 *
 * @return         Result
 *                 - BK_OK: success
 *                 - other: failed
 */
void doorbell_audio_device_deinit(void);

#endif
