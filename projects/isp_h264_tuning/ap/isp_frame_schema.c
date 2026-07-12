#define ISP_FRAME_FORM_JSON \
    "{\"id\":\"isp-wifi-config\",\"title\":\"ISP Capture Config\"," \
    "\"desc\":\"ISP startup parameters aligned with isp_dump_tool. Format (NV12/RAW10) is runtime tunable.\"," \
    "\"groups\":[" \
    "{\"id\":\"capture\",\"title\":\"Capture\",\"fields\":[" \
    "{\"type\":\"number\",\"id\":\"rxTimeout\",\"label\":\"RX timeout (ms)\",\"default\":1000000," \
    "\"min\":1,\"max\":1000000,\"description\":\"ISP frame read timeout in milliseconds\"}," \
    "{\"type\":\"number\",\"id\":\"sensorWidth\",\"label\":\"sensorWidth\",\"default\":2304,\"min\":1,\"max\":8192,\"unit\":\"pixel\"}," \
    "{\"type\":\"number\",\"id\":\"sensorHeight\",\"label\":\"sensorHeight\",\"default\":1296,\"min\":1,\"max\":8192,\"unit\":\"pixel\"}," \
    "{\"type\":\"number\",\"id\":\"ispW\",\"label\":\"ISP W\",\"default\":2304,\"min\":1,\"max\":8192,\"unit\":\"pixel\"}," \
    "{\"type\":\"number\",\"id\":\"ispH\",\"label\":\"ISP H\",\"default\":1296,\"min\":1,\"max\":8192,\"unit\":\"pixel\"}," \
    "{\"type\":\"select\",\"id\":\"format\",\"label\":\"Format\",\"default\":23," \
    "\"options\":[{\"label\":\"NV12\",\"value\":23},{\"label\":\"RAW10\",\"value\":21}]}," \
    "{\"type\":\"select\",\"id\":\"pattern\",\"label\":\"Pattern\",\"default\":\"RGGB\"," \
    "\"options\":[{\"label\":\"RGGB\",\"value\":\"RGGB\"},{\"label\":\"GRBG\",\"value\":\"GRBG\"}," \
    "{\"label\":\"GBRG\",\"value\":\"GBRG\"},{\"label\":\"BGGR\",\"value\":\"BGGR\"}]}]}" \
    "],\"submit\":{\"label\":\"Apply\",\"method\":\"set_config\",\"build\":\"tree\"}}"

#define ISP_FRAME_RATE_CTRL_SCHEMA_JSON \
    "{\"supported\":[" \
    "{\"id\":\"rxTimeout\",\"type\":\"u32\",\"min\":1,\"max\":1000000,\"description\":\"Frame read timeout (ms)\"}," \
    "{\"id\":\"sensorWidth\",\"type\":\"u16\",\"min\":1,\"max\":8192,\"description\":\"Sensor input width\"}," \
    "{\"id\":\"sensorHeight\",\"type\":\"u16\",\"min\":1,\"max\":8192,\"description\":\"Sensor input height\"}," \
    "{\"id\":\"ispW\",\"type\":\"u16\",\"min\":1,\"max\":8192,\"description\":\"ISP output width\"}," \
    "{\"id\":\"ispH\",\"type\":\"u16\",\"min\":1,\"max\":8192,\"description\":\"ISP output height\"}," \
    "{\"id\":\"format\",\"type\":\"u16\",\"enum\":[21,23],\"description\":\"Pixel format code (21=RAW10, 23=NV12)\"}," \
    "{\"id\":\"pattern\",\"type\":\"string\",\"enum\":[\"RGGB\",\"GRBG\",\"GBRG\",\"BGGR\"],\"description\":\"Bayer pattern for RAW10\"}" \
    "],\"note\":\"Apply via set_config before turnOn/start_encode. Only format supports runtime change after ISP is up.\"}"

const char *isp_frame_session_get_form_json(void)
{
    return ISP_FRAME_FORM_JSON;
}

const char *isp_frame_session_get_rate_ctrl_schema_json(void)
{
    return ISP_FRAME_RATE_CTRL_SCHEMA_JSON;
}
