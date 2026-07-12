#define H264E_STREAM_FORM_JSON \
    "{\"id\":\"h264e-stream-config\",\"title\":\"H264E Stream Encoder Config\"," \
    "\"desc\":\"Dynamic form for PC tool. Submit params through JSON-RPC set_config.\"," \
    "\"groups\":[" \
    "{\"title\":\"Service\",\"fields\":[{\"type\":\"radio\",\"id\":\"mode\",\"label\":\"Service Mode\"," \
    "\"default\":\"tcp\",\"required\":true,\"options\":[{\"label\":\"TCP\",\"value\":\"tcp\"},{\"label\":\"UDP\",\"value\":\"udp\"}]}]}," \
    "{\"id\":\"video\",\"title\":\"Video\",\"fields\":[" \
    "{\"type\":\"number\",\"id\":\"width\",\"label\":\"Width\",\"default\":2304,\"readonly\":false,\"min\":320,\"max\":2304,\"unit\":\"pixel\"}," \
    "{\"type\":\"number\",\"id\":\"height\",\"label\":\"Height\",\"default\":1296,\"readonly\":false,\"min\":240,\"max\":1296,\"unit\":\"pixel\"}," \
    "{\"type\":\"select\",\"id\":\"fps\",\"label\":\"FPS\",\"default\":20,\"unit\":\"fps\",\"requiresRestart\":true," \
    "\"options\":[{\"label\":\"10 fps\",\"value\":10},{\"label\":\"15 fps\",\"value\":15},{\"label\":\"20 fps\",\"value\":20},{\"label\":\"25 fps\",\"value\":25}]}," \
    "{\"type\":\"number\",\"id\":\"bitrateKbps\",\"label\":\"Target Bitrate\",\"default\":1200,\"min\":64,\"max\":8000,\"step\":64,\"unit\":\"kbps\"}," \
    "{\"type\":\"number\",\"id\":\"gopFrameCount\",\"label\":\"GOP Frame Count\",\"default\":20,\"min\":1,\"max\":300,\"step\":1,\"runtimeWritable\":true}]}," \
    "{\"id\":\"rateCtrl\",\"title\":\"Writable Rate Control\",\"fields\":[" \
    "{\"type\":\"number\",\"id\":\"bitrate\",\"label\":\"Bitrate\",\"description\":\"Unit bps. 0 means fixed QP mode.\"," \
    "\"default\":1200000,\"min\":0,\"max\":8000000,\"step\":64000,\"unit\":\"bps\"}," \
    "{\"type\":\"number\",\"id\":\"qpMinI\",\"label\":\"I Min QP\",\"default\":18,\"min\":0,\"max\":51,\"step\":1}," \
    "{\"type\":\"number\",\"id\":\"qpMaxI\",\"label\":\"I Max QP\",\"default\":40,\"min\":0,\"max\":51,\"step\":1}," \
    "{\"type\":\"number\",\"id\":\"qpMinP\",\"label\":\"P Min QP\",\"default\":22,\"min\":0,\"max\":51,\"step\":1}," \
    "{\"type\":\"number\",\"id\":\"qpMaxP\",\"label\":\"P Max QP\",\"default\":44,\"min\":0,\"max\":51,\"step\":1}]}," \
    "{\"id\":\"vcencRateCtrl\",\"title\":\"Full VCEncRateCtrl Fields\"," \
    "\"desc\":\"Runtime-writable fields applied through the internal VCEnc rate-control path when the encoder is opened. Obsoleted fields are not exposed.\"," \
    "\"fields\":[" \
    "{\"type\":\"number\",\"id\":\"crf\",\"label\":\"crf\",\"readonly\":false,\"min\":0,\"max\":51}," \
    "{\"type\":\"switch\",\"id\":\"pictureRc\",\"label\":\"pictureRc\",\"readonly\":false}," \
    "{\"type\":\"select\",\"id\":\"ctbRc\",\"label\":\"ctbRc\",\"readonly\":false,\"options\":[{\"label\":\"disable\",\"value\":0},{\"label\":\"subjective\",\"value\":1},{\"label\":\"precise\",\"value\":2},{\"label\":\"mixed\",\"value\":3}]}," \
    "{\"type\":\"select\",\"id\":\"blockRCSize\",\"label\":\"blockRCSize\",\"readonly\":false,\"options\":[{\"label\":\"64x64\",\"value\":0},{\"label\":\"32x32\",\"value\":1},{\"label\":\"16x16\",\"value\":2}]}," \
    "{\"type\":\"switch\",\"id\":\"pictureSkip\",\"label\":\"pictureSkip\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"qpHdr\",\"label\":\"qpHdr\",\"readonly\":false,\"min\":-1,\"max\":51}," \
    "{\"type\":\"number\",\"id\":\"qpMinPB\",\"label\":\"qpMinPB\",\"readonly\":false,\"min\":0,\"max\":51}," \
    "{\"type\":\"number\",\"id\":\"qpMaxPB\",\"label\":\"qpMaxPB\",\"readonly\":false,\"min\":0,\"max\":51}," \
    "{\"type\":\"number\",\"id\":\"qpMinI\",\"label\":\"qpMinI\",\"readonly\":false,\"min\":0,\"max\":51}," \
    "{\"type\":\"number\",\"id\":\"qpMaxI\",\"label\":\"qpMaxI\",\"readonly\":false,\"min\":0,\"max\":51}," \
    "{\"type\":\"number\",\"id\":\"bitPerSecond\",\"label\":\"bitPerSecond\",\"readonly\":false,\"default\":1200000,\"min\":10000,\"unit\":\"bps\"}," \
    "{\"type\":\"number\",\"id\":\"cpbMaxRate\",\"label\":\"cpbMaxRate\",\"readonly\":false,\"unit\":\"bps\"}," \
    "{\"type\":\"switch\",\"id\":\"fillerData\",\"label\":\"fillerData\",\"readonly\":false}," \
    "{\"type\":\"switch\",\"id\":\"hrd\",\"label\":\"hrd\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"hrdCpbSize\",\"label\":\"hrdCpbSize\",\"readonly\":false,\"unit\":\"bit\"}," \
    "{\"type\":\"number\",\"id\":\"bitrateWindow\",\"label\":\"bitrateWindow\",\"readonly\":false,\"default\":20,\"min\":1,\"max\":300,\"unit\":\"frame\"}," \
    "{\"type\":\"number\",\"id\":\"intraQpDelta\",\"label\":\"intraQpDelta\",\"readonly\":false,\"min\":-12,\"max\":12}," \
    "{\"type\":\"number\",\"id\":\"fixedIntraQp\",\"label\":\"fixedIntraQp\",\"readonly\":false,\"min\":0,\"max\":51}," \
    "{\"type\":\"number\",\"id\":\"bitVarRangeP\",\"label\":\"bitVarRangeP\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"bitVarRangeB\",\"label\":\"bitVarRangeB\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"tolMovingBitRate\",\"label\":\"tolMovingBitRate\",\"readonly\":false,\"min\":0,\"max\":2000}," \
    "{\"type\":\"number\",\"id\":\"monitorFrames\",\"label\":\"monitorFrames\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"targetPicSize\",\"label\":\"targetPicSize\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"u32StaticSceneIbitPercent\",\"label\":\"u32StaticSceneIbitPercent\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"rcQpDeltaRange\",\"label\":\"rcQpDeltaRange\",\"readonly\":false,\"min\":0,\"max\":51}," \
    "{\"type\":\"number\",\"id\":\"rcBaseMBComplexity\",\"label\":\"rcBaseMBComplexity\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"picQpDeltaMin\",\"label\":\"picQpDeltaMin\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"picQpDeltaMax\",\"label\":\"picQpDeltaMax\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"longTermQpDelta\",\"label\":\"longTermQpDelta\",\"readonly\":false,\"min\":-51,\"max\":51}," \
    "{\"type\":\"switch\",\"id\":\"vbr\",\"label\":\"vbr\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"rcMode\",\"label\":\"rcMode\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"tolCtbRcInter\",\"label\":\"tolCtbRcInter\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"tolCtbRcIntra\",\"label\":\"tolCtbRcIntra\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"tolRcUnderflow\",\"label\":\"tolRcUnderflow\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"maxIprop\",\"label\":\"maxIprop\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"minIprop\",\"label\":\"minIprop\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"changePos\",\"label\":\"changePos\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"ctbRcRowQpStep\",\"label\":\"ctbRcRowQpStep\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"ctbRcRowQpDeltaRange\",\"label\":\"ctbRcRowQpDeltaRange\",\"readonly\":false}," \
    "{\"type\":\"switch\",\"id\":\"ctbRcQpDeltaReverse\",\"label\":\"ctbRcQpDeltaReverse\",\"readonly\":false}," \
    "{\"type\":\"number\",\"id\":\"frameRateNum\",\"label\":\"frameRateNum\",\"readonly\":false,\"default\":20,\"min\":1,\"max\":1048575}," \
    "{\"type\":\"number\",\"id\":\"frameRateDenom\",\"label\":\"frameRateDenom\",\"readonly\":false,\"default\":1,\"min\":1}," \
    "{\"type\":\"switch\",\"id\":\"hieQpDeltaEnable\",\"label\":\"hieQpDeltaEnable\",\"readonly\":false}]}," \
    "{\"id\":\"ctrl\",\"title\":\"Control\",\"fields\":[{\"type\":\"switch\",\"id\":\"forceIdr\",\"label\":\"Force IDR after apply\",\"default\":true}]}" \
    "],\"submit\":{\"label\":\"Apply\",\"method\":\"set_config\",\"build\":\"tree\"}}"

#define H264E_STREAM_RATE_CTRL_SCHEMA_JSON \
    "{\"supported\":[" \
    "{\"id\":\"bitrate\",\"vcenc\":\"bitPerSecond\",\"type\":\"u32\",\"unit\":\"bps\",\"description\":\"0=fixed_qp, nonzero=bitrate_rc\"}," \
    "{\"id\":\"qpMinI\",\"vcenc\":\"qpMinI\",\"type\":\"u32\",\"min\":0,\"max\":51,\"description\":\"I-frame min QP\"}," \
    "{\"id\":\"qpMaxI\",\"vcenc\":\"qpMaxI\",\"type\":\"u32\",\"min\":0,\"max\":51,\"description\":\"I-frame max QP\"}," \
    "{\"id\":\"qpMinP\",\"vcenc\":\"qpMinPB\",\"type\":\"u32\",\"min\":0,\"max\":51,\"description\":\"P-frame min QP\"}," \
    "{\"id\":\"qpMaxP\",\"vcenc\":\"qpMaxPB\",\"type\":\"u32\",\"min\":0,\"max\":51,\"description\":\"P-frame max QP\"}" \
    "],\"notExposed\":[" \
    "{\"id\":\"bitVarRangeI\",\"reason\":\"obsoleted_by_vcenc\"}," \
    "{\"id\":\"smoothPsnrInGOP\",\"reason\":\"obsoleted_by_vcenc\"}" \
    "]," \
    "\"vcencAllFields\":[" \
    "\"crf\",\"pictureRc\",\"ctbRc\",\"blockRCSize\",\"pictureSkip\",\"qpHdr\"," \
    "\"qpMinPB\",\"qpMaxPB\",\"qpMinI\",\"qpMaxI\",\"bitPerSecond\",\"cpbMaxRate\"," \
    "\"fillerData\",\"hrd\",\"hrdCpbSize\",\"bitrateWindow\",\"intraQpDelta\"," \
    "\"fixedIntraQp\",\"bitVarRangeP\",\"bitVarRangeB\"," \
    "\"tolMovingBitRate\",\"monitorFrames\",\"targetPicSize\"," \
    "\"u32StaticSceneIbitPercent\",\"rcQpDeltaRange\",\"rcBaseMBComplexity\"," \
    "\"picQpDeltaMin\",\"picQpDeltaMax\",\"longTermQpDelta\",\"vbr\",\"rcMode\"," \
    "\"tolCtbRcInter\",\"tolCtbRcIntra\",\"tolRcUnderflow\",\"maxIprop\",\"minIprop\"," \
    "\"changePos\",\"ctbRcRowQpStep\",\"ctbRcRowQpDeltaRange\",\"ctbRcQpDeltaReverse\"," \
    "\"frameRateNum\",\"frameRateDenom\",\"hieQpDeltaEnable\"]," \
    "\"note\":\"Full VCEnc fields are exposed except fields marked Obsoleted in VCEncRateCtrl.\"}"

const char *h264e_stream_session_get_form_json(void)
{
    return H264E_STREAM_FORM_JSON;
}

const char *h264e_stream_session_get_rate_ctrl_schema_json(void)
{
    return H264E_STREAM_RATE_CTRL_SCHEMA_JSON;
}
