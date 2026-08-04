#include <common/bk_include.h>

#include "cJSON.h"

#include "doorbell_rpc_internal.h"

#define TAG "db-rpc-sol"

/*
 * doorbell.solution.getConfig
 *
 * Report this device's supported solution configuration to the app, per the
 * IoT protocol section 4.1.16 (doorbell.solution.getConfig) and the
 * solutionConfig / methodConfig definitions. The response shape is:
 *
 *   result.data = {
 *     "solutionCount": <int, == solutions.length>,
 *     "solutions": [ solutionConfig, ... ]
 *   }
 *   solutionConfig = { "name": <str>, "configs": [ methodConfig, ... ] }
 *   methodConfig   = { "method": <str>, "params": { ... } }   // params optional
 *
 * configs enumerates the methods this solution supports, each with a default
 * parameter template the app can use as-is. Values mirror the actual
 * video_intercom board pipeline: gc2053 MIPI sensor -> ISP MP 1280x720 h264
 * uplink; hx8399c 1080x1920 MIPI panel; 16 kHz PCM two-way audio (AEC on);
 * h264 downlink image stream (1280x720); ISP SP 640x360 for local PIP only.
 * See ap/ap_main.c for the board setup.
 */

/* Append a parameter-less methodConfig {"method": m} to the configs array. */
static void cfg_add(cJSON *configs, const char *method)
{
    cJSON *mc = cJSON_CreateObject();

    if (mc == NULL)
    {
        return;
    }
    cJSON_AddStringToObject(mc, "method", method);
    cJSON_AddItemToArray(configs, mc);
}

/* Append methodConfig {"method": m, "params": p} to configs; takes ownership
 * of p (a NULL p degrades to a parameter-less entry). */
static void cfg_add_p(cJSON *configs, const char *method, cJSON *params)
{
    cJSON *mc = cJSON_CreateObject();

    if (mc == NULL)
    {
        if (params != NULL)
        {
            cJSON_Delete(params);
        }
        return;
    }
    cJSON_AddStringToObject(mc, "method", method);
    if (params != NULL)
    {
        cJSON_AddItemToObject(mc, "params", params);
    }
    cJSON_AddItemToArray(configs, mc);
}

/* --- per-method default parameter templates (device real defaults) --- */

static cJSON *p_service_set_type(void)
{
    cJSON *p = cJSON_CreateObject();

    if (p != NULL)
    {
        cJSON_AddStringToObject(p, "serviceType", "tcp");
    }
    return p;
}

static cJSON *p_session_keepalive(void)
{
    cJSON *p = cJSON_CreateObject();

    if (p != NULL)
    {
        cJSON_AddNumberToObject(p, "intervalMs", 30000);
    }
    return p;
}

static cJSON *p_camera_turn_off(void)
{
    cJSON *p = cJSON_CreateObject();

    if (p != NULL)
    {
        cJSON_AddStringToObject(p, "target", "all");
    }
    return p;
}

static cJSON *p_audio_turn_on(void)
{
    cJSON *p = cJSON_CreateObject();

    if (p == NULL)
    {
        return NULL;
    }
    cJSON_AddNumberToObject(p, "aec", 1);
    cJSON_AddStringToObject(p, "micType", "dmic");
    cJSON_AddStringToObject(p, "spkType", "speaker");
    cJSON_AddNumberToObject(p, "recordSampleRate", 16000);
    cJSON_AddNumberToObject(p, "playSampleRate", 16000);
    cJSON_AddStringToObject(p, "recordFmt", "pcm");
    cJSON_AddStringToObject(p, "playFmt", "pcm");
    cJSON_AddNumberToObject(p, "asr", 0);
    return p;
}

static cJSON *p_audio_set_acoustics(void)
{
    cJSON *p = cJSON_CreateObject();

    if (p == NULL)
    {
        return NULL;
    }
    cJSON_AddStringToObject(p, "name", "echoDepth");
    cJSON_AddNumberToObject(p, "value", 10);
    return p;
}

static cJSON *p_lcd_turn_on(void)
{
    cJSON *p = cJSON_CreateObject();
    cJSON *cfg;
    cJSON *mipi;

    if (p == NULL)
    {
        return NULL;
    }
    cJSON_AddStringToObject(p, "lcdType", "mipi");

    cfg = cJSON_CreateObject();
    mipi = cJSON_CreateObject();
    if (cfg == NULL || mipi == NULL)
    {
        if (cfg != NULL) cJSON_Delete(cfg);
        if (mipi != NULL) cJSON_Delete(mipi);
        return p;
    }
    cJSON_AddStringToObject(mipi, "sensorName", "hx8399c");
    cJSON_AddNumberToObject(mipi, "sensorId", 1);
    cJSON_AddNumberToObject(mipi, "width", 1080);
    cJSON_AddNumberToObject(mipi, "height", 1920);
    cJSON_AddItemToObject(cfg, "mipi", mipi);
    cJSON_AddItemToObject(p, "lcdConfig", cfg);
    return p;
}

static cJSON *p_image_set_recv_config(void)
{
    cJSON *p = cJSON_CreateObject();
    cJSON *fc;
    cJSON *h264;

    if (p == NULL)
    {
        return NULL;
    }
    cJSON_AddStringToObject(p, "imageFormat", "h264");

    fc = cJSON_CreateObject();
    h264 = cJSON_CreateObject();
    if (fc == NULL || h264 == NULL)
    {
        if (fc != NULL) cJSON_Delete(fc);
        if (h264 != NULL) cJSON_Delete(h264);
        return p;
    }
    cJSON_AddNumberToObject(h264, "width", 1280);
    cJSON_AddNumberToObject(h264, "height", 720);
    /* Downlink (phone -> device) capture rate negotiated with the App. Once the
     * App sends a clean contiguous IPPP stream (no mid-GOP encoded-frame drops),
     * decode is stable with no reference-chain breaks, so the rate can be raised
     * from 10 to 20 fps for smoother playback. The decode+GPU+compose stage must
     * still keep up. At 1280x720, 15fps leaves more decode budget per frame than
     * 20fps. */
    cJSON_AddNumberToObject(h264, "fps", 15);
    cJSON_AddNumberToObject(h264, "pFrameCount", 29);
    cJSON_AddItemToObject(fc, "h264", h264);
    cJSON_AddItemToObject(p, "formatConfig", fc);
    return p;
}

static cJSON *p_camera_turn_on(void)
{
    cJSON *p = cJSON_CreateObject();
    cJSON *streams;
    cJSON *stream;
    cJSON *ccfg;
    cJSON *mipi;

    if (p == NULL)
    {
        return NULL;
    }
    cJSON_AddNumberToObject(p, "streamCount", 1);

    streams = cJSON_CreateArray();
    stream = cJSON_CreateObject();
    ccfg = cJSON_CreateObject();
    mipi = cJSON_CreateObject();
    if (streams == NULL || stream == NULL || ccfg == NULL || mipi == NULL)
    {
        if (streams != NULL) cJSON_Delete(streams);
        if (stream != NULL) cJSON_Delete(stream);
        if (ccfg != NULL) cJSON_Delete(ccfg);
        if (mipi != NULL) cJSON_Delete(mipi);
        return p;
    }
    cJSON_AddStringToObject(mipi, "sensorName", "gc2053");
    cJSON_AddNumberToObject(mipi, "sensorId", 1);
    cJSON_AddNumberToObject(mipi, "width", 1280);
    cJSON_AddNumberToObject(mipi, "height", 720);
    cJSON_AddNumberToObject(mipi, "fps", 15);
    cJSON_AddStringToObject(mipi, "videoFormat", "h264");
    cJSON_AddNumberToObject(mipi, "rotate", 0);
    cJSON_AddItemToObject(ccfg, "mipi", mipi);

    cJSON_AddStringToObject(stream, "cameraType", "mipi");
    cJSON_AddItemToObject(stream, "cameraConfig", ccfg);
    cJSON_AddItemToArray(streams, stream);
    cJSON_AddItemToObject(p, "streams", streams);
    return p;
}

/* doorbell.solution.getConfig : report device solution config to the app. */
bk_err_t doorbell_rpc_solution_get_config(cJSON *params, cJSON *id)
{
    cJSON *result;
    cJSON *data;
    cJSON *solutions;
    cJSON *solution;
    cJSON *configs;

    (void)params;

    result = cJSON_CreateObject();
    data = cJSON_CreateObject();
    solutions = cJSON_CreateArray();
    solution = cJSON_CreateObject();
    configs = cJSON_CreateArray();
    if (result == NULL || data == NULL || solutions == NULL ||
        solution == NULL || configs == NULL)
    {
        if (result) cJSON_Delete(result);
        if (data) cJSON_Delete(data);
        if (solutions) cJSON_Delete(solutions);
        if (solution) cJSON_Delete(solution);
        if (configs) cJSON_Delete(configs);
        return doorbell_rpc_send_error(id, DB_RPC_ERR_INTERNAL, "no mem", NULL);
    }

    /* Supported methods + default parameter templates, in protocol order. */
    cfg_add_p(configs, "doorbell.service.setType",             p_service_set_type());
    cfg_add_p(configs, "doorbell.session.setKeepAlive",        p_session_keepalive());
    cfg_add_p(configs, "doorbell.camera.turnOff",              p_camera_turn_off());
    cfg_add  (configs, "doorbell.camera.getStatus");
    cfg_add  (configs, "doorbell.audio.getStatus");
    cfg_add_p(configs, "doorbell.audio.turnOn",                p_audio_turn_on());
    cfg_add  (configs, "doorbell.audio.turnOff");
    cfg_add_p(configs, "doorbell.lcd.turnOn",                  p_lcd_turn_on());
    cfg_add  (configs, "doorbell.lcd.turnOff");
    cfg_add  (configs, "doorbell.lcd.getStatus");
    cfg_add_p(configs, "doorbell.audio.setAcoustics",          p_audio_set_acoustics());
    cfg_add  (configs, "doorbell.misc.ping");
    cfg_add  (configs, "doorbell.notify.heartbeat");
    cfg_add_p(configs, "doorbell.imageStream.setReceiveConfig", p_image_set_recv_config());
    cfg_add_p(configs, "doorbell.camera.turnOn",               p_camera_turn_on());

    cJSON_AddStringToObject(solution, "name", "doorbell");
    cJSON_AddItemToObject(solution, "configs", configs);

    cJSON_AddItemToArray(solutions, solution);
    cJSON_AddNumberToObject(data, "solutionCount", 1);
    cJSON_AddItemToObject(data, "solutions", solutions);
    cJSON_AddItemToObject(result, "data", data);

    return doorbell_rpc_send_result(id, result);
}
