
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>

#include <components/audio_param_ctrl.h>
#include <components/bk_audio/audio_algorithms/aec_v3_algorithm_v2.h>

#include "audio_param_hooks.h"

// hardware speaker has two version, the new black speaker box is set to 1, else set  HARDWARE_SPEAKER_VER to 0 in kconfig.projbuild
/// customer eq parameter
#define EQTotalNum 3
#define EQGAIN 16384

/* ============================ sampleRate 8K EQ params ============================ */
//E0_freq_500_gain_n15_qval_1_type_1_LS
#define EQ0_8K 1
#define EQ0A0_8K -1357286
#define EQ0A1_8K 589778
#define EQ0B0_8K 858142
#define EQ0B1_8K -1472829
#define EQ0B2_8K 664668
#define EQ0FREQ_8K 0x43fa0000
#define EQ0GAIN_8K 0xc1700000
#define EQ0QVAL_8K 0x3f800000
#define EQ0FTYPE_8K 0x01

//E1_freq_700_gain_n1_qval_1_type_0_PK
#define EQ1_8K 1
#define EQ1A0_8K -1400545
#define EQ1A1_8K 594021
#define EQ1B0_8K 1023859
#define EQ1B1_8K -1400545
#define EQ1B2_8K 618737
#define EQ1FREQ_8K 0x442f0000
#define EQ1GAIN_8K 0xbf800000
#define EQ1QVAL_8K 0x3f800000
#define EQ1FTYPE_8K 0x00

//E2_freq_3000_gain_n45_qval_0.7_type_3_LP
#define EQ2_8K 1
#define EQ2A0_8K 985272
#define EQ2A1_8K 344809
#define EQ2B0_8K 594664
#define EQ2B1_8K 1189329
#define EQ2B2_8K 594664
#define EQ2FREQ_8K 0x453b8000
#define EQ2GAIN_8K 0xc2340000
#define EQ2QVAL_8K 0x3f333333
#define EQ2FTYPE_8K 0x03

#define EQSAMP_8K    0x1f40
#define EQFGAIN_8K   0x00000000

#define DEFAULT_DOORBELL_8K_EQ_CAL_PARA() {        \
    .app_eq_en = 1,                                \
    .eq_en = 1,                                    \
    .filters = EQTotalNum,                         \
    .globle_gain = EQGAIN,                         \
    .eq_para[0].a[0] = -EQ0A0_8K,                  \
    .eq_para[0].a[1] = -EQ0A1_8K,                  \
    .eq_para[0].b[0] = EQ0B0_8K,                   \
    .eq_para[0].b[1] = EQ0B1_8K,                   \
    .eq_para[0].b[2] = EQ0B2_8K,                   \
    .eq_para[1].a[0] = -EQ1A0_8K,                  \
    .eq_para[1].a[1] = -EQ1A1_8K,                  \
    .eq_para[1].b[0] = EQ1B0_8K,                   \
    .eq_para[1].b[1] = EQ1B1_8K,                   \
    .eq_para[1].b[2] = EQ1B2_8K,                   \
    .eq_para[2].a[0] = -EQ2A0_8K,                  \
    .eq_para[2].a[1] = -EQ2A1_8K,                  \
    .eq_para[2].b[0] = EQ2B0_8K,                   \
    .eq_para[2].b[1] = EQ2B1_8K,                   \
    .eq_para[2].b[2] = EQ2B2_8K,                   \
    .eq_load.f_gain = EQFGAIN_8K,                  \
    .eq_load.samplerate = EQSAMP_8K,               \
    .eq_load.eq_load_para[0].freq = EQ0FREQ_8K,    \
    .eq_load.eq_load_para[0].gain = EQ0GAIN_8K,    \
    .eq_load.eq_load_para[0].q_val = EQ0QVAL_8K,   \
    .eq_load.eq_load_para[0].type = EQ0FTYPE_8K,   \
    .eq_load.eq_load_para[0].enable = EQ0_8K,      \
    .eq_load.eq_load_para[1].freq = EQ1FREQ_8K,    \
    .eq_load.eq_load_para[1].gain = EQ1GAIN_8K,    \
    .eq_load.eq_load_para[1].q_val = EQ1QVAL_8K,   \
    .eq_load.eq_load_para[1].type = EQ1FTYPE_8K,   \
    .eq_load.eq_load_para[1].enable = EQ1_8K,      \
    .eq_load.eq_load_para[2].freq = EQ2FREQ_8K,    \
    .eq_load.eq_load_para[2].gain = EQ2GAIN_8K,    \
    .eq_load.eq_load_para[2].q_val = EQ2QVAL_8K,   \
    .eq_load.eq_load_para[2].type = EQ2FTYPE_8K,   \
    .eq_load.eq_load_para[2].enable = EQ2_8K,      \
}

/* ============================ sampleRate 16K EQ params =========================== */
//E0_freq_500_gain_n15_qval_1_type_1_LS
#define EQ0_16K 1
#define EQ0A0_16K -1744239
#define EQ0A1_16K 777799
#define EQ0B0_16K 953397
#define EQ0B1_16K -1778004
#define EQ0B2_16K 839212
#define EQ0FREQ_16K 0x43fa0000
#define EQ0GAIN_16K 0xc1700000
#define EQ0QVAL_16K 0x3f800000
#define EQ0FTYPE_16K 0x01

//E1_freq_700_gain_n1_qval_1_type_0_PK
#define EQ1_16K 1
#define EQ1A0_16K -1764716
#define EQ1A1_16K 784980
#define EQ1B0_16K 1034243
#define EQ1B1_16K -1764716
#define EQ1B2_16K 799312
#define EQ1FREQ_16K 0x442f0000
#define EQ1GAIN_16K 0xbf800000
#define EQ1QVAL_16K 0x3f800000
#define EQ1FTYPE_16K 0x00

//E2_freq_3000_gain_n45_qval_0.7_type_3_LP
#define EQ2_16K 1
#define EQ2A0_16K -483487
#define EQ2A1_16K 214834
#define EQ2B0_16K 194980
#define EQ2B1_16K 389961
#define EQ2B2_16K 194980
#define EQ2FREQ_16K 0x453b8000
#define EQ2GAIN_16K 0xc2340000
#define EQ2QVAL_16K 0x3f333333
#define EQ2FTYPE_16K 0x03

#define EQSAMP_16K   0x3e80
#define EQFGAIN_16K  0x00000000

#define DEFAULT_DOORBELL_16K_EQ_CAL_PARA() {       \
    .app_eq_en = 1,                                \
    .eq_en = 1,                                    \
    .filters = EQTotalNum,                         \
    .globle_gain = EQGAIN,                         \
    .eq_para[0].a[0] = -EQ0A0_16K,                 \
    .eq_para[0].a[1] = -EQ0A1_16K,                 \
    .eq_para[0].b[0] = EQ0B0_16K,                  \
    .eq_para[0].b[1] = EQ0B1_16K,                  \
    .eq_para[0].b[2] = EQ0B2_16K,                  \
    .eq_para[1].a[0] = -EQ1A0_16K,                 \
    .eq_para[1].a[1] = -EQ1A1_16K,                 \
    .eq_para[1].b[0] = EQ1B0_16K,                  \
    .eq_para[1].b[1] = EQ1B1_16K,                  \
    .eq_para[1].b[2] = EQ1B2_16K,                  \
    .eq_para[2].a[0] = -EQ2A0_16K,                 \
    .eq_para[2].a[1] = -EQ2A1_16K,                 \
    .eq_para[2].b[0] = EQ2B0_16K,                  \
    .eq_para[2].b[1] = EQ2B1_16K,                  \
    .eq_para[2].b[2] = EQ2B2_16K,                  \
    .eq_load.f_gain = EQFGAIN_16K,                 \
    .eq_load.samplerate = EQSAMP_16K,              \
    .eq_load.eq_load_para[0].freq = EQ0FREQ_16K,   \
    .eq_load.eq_load_para[0].gain = EQ0GAIN_16K,   \
    .eq_load.eq_load_para[0].q_val = EQ0QVAL_16K,  \
    .eq_load.eq_load_para[0].type = EQ0FTYPE_16K,  \
    .eq_load.eq_load_para[0].enable = EQ0_16K,     \
    .eq_load.eq_load_para[1].freq = EQ1FREQ_16K,   \
    .eq_load.eq_load_para[1].gain = EQ1GAIN_16K,   \
    .eq_load.eq_load_para[1].q_val = EQ1QVAL_16K,  \
    .eq_load.eq_load_para[1].type = EQ1FTYPE_16K,  \
    .eq_load.eq_load_para[1].enable = EQ1_16K,     \
    .eq_load.eq_load_para[2].freq = EQ2FREQ_16K,   \
    .eq_load.eq_load_para[2].gain = EQ2GAIN_16K,   \
    .eq_load.eq_load_para[2].q_val = EQ2QVAL_16K,  \
    .eq_load.eq_load_para[2].type = EQ2FTYPE_16K,  \
    .eq_load.eq_load_para[2].enable = EQ2_16K,     \
}

#define CUST_AEC_V3_CONFIG_VOICE()                                       \
{                                                                        \
    .app_aec_en = 1,                                                     \
    .aec_enable = 1,                                                     \
    .init_flags = 0x1f,                                                  \
    .ec_filter = 0x7,                                                    \
    .ec_depth = 0x2,                                                     \
    .mic_delay = 16,                                                     \
    .drc_gain = 0,                                                       \
    .voice_vol = 0xe,                                                    \
    .ref_scale = 0,                                                      \
    .ns_level = 0x5,                                                     \
    .ns_para = 0x2,                                                      \
    .ns_filter = 0x7,                                                    \
    .ns_type = NS_TRADITION,                                             \
    .vad_enable = 0,                                                     \
    .vad_start_threshold = 480,                                          \
    .vad_stop_threshold = 960,                                           \
    .vad_silence_threshold = 320,                                        \
    .vad_eng_threshold =2000,                                            \
    .dual_mic_enable = 0,                                                \
    .dual_mic_distance = 21,                                             \
}

#define CUST_SYS_CONFIG_VOICE()                                          \
{                                                                        \
    .app_sys_en = 1,                                                     \
    .mic0_digital_gain=16,                                               \
    .mic0_analog_gain =20,                                               \
    .mic1_digital_gain=16,                                               \
    .mic1_analog_gain =20,                                               \
    .mic2_digital_gain=16,                                               \
    .mic2_analog_gain =20,                                               \
    .spk0_digital_gain = -16,                                            \
    .spk0_analog_gain  = 4,                                              \
}

static app_aud_para_t app_aud_cust_voice_8k_para = {
    .service_type    = AUD_SERVICE_DOORBELL_VOC,
    .sys_config     = CUST_SYS_CONFIG_VOICE(),
    .aec_v3_config  = CUST_AEC_V3_CONFIG_VOICE(),
    .eq_dl_config   = DEFAULT_DOORBELL_8K_EQ_CAL_PARA(),
};

static app_aud_para_t app_aud_cust_voice_16k_para = {
    .service_type    = AUD_SERVICE_DOORBELL_VOC,
    .sys_config     = CUST_SYS_CONFIG_VOICE(),
    .aec_v3_config  = CUST_AEC_V3_CONFIG_VOICE(),
    .eq_dl_config   = DEFAULT_DOORBELL_16K_EQ_CAL_PARA(),
};

/* Pick the preset matching the running sample rate. EQ lives on the DL
 * (playback) path, so the caller passes the speaker sample rate. */
static app_aud_para_t *get_app_aud_cust_para(app_aud_service_type_t service_type, uint32_t sample_rate)
{
    switch (service_type)
    {
        case AUD_SERVICE_DOORBELL_VOC:
        case AUD_SERVICE_AI_VOC:
            if (sample_rate == 8000) {
                app_aud_cust_voice_8k_para.service_type = service_type;
                return &app_aud_cust_voice_8k_para;
            }
            app_aud_cust_voice_16k_para.service_type = service_type;
            return &app_aud_cust_voice_16k_para;
        default:
            return NULL;
    }
}

/*
 * adapter = NULL: for SDK voice service the param-ctrl framework already
 * resolves mic/spk/eq/aec elements from the voice handle via its built-in path,
 * so no project-side adapter is needed. bind hands the preset to the debug tool
 * and applies the enabled default sys/aec/eq parameters.
 */
void media_audio_param_bind_voc_handle(void *voc_handle, uint32_t sample_rate)
{
    bk_app_aud_service_bind(AUD_SERVICE_DOORBELL_VOC, voc_handle, NULL, NULL,
                            get_app_aud_cust_para(AUD_SERVICE_DOORBELL_VOC, sample_rate));
}

void media_audio_param_unbind_voc_handle(void)
{
    bk_app_aud_service_unbind(AUD_SERVICE_DOORBELL_VOC);
}
