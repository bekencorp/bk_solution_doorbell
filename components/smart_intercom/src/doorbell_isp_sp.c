/*
 * ISP SP channel capture helper for the downlink PIP self-view.
 *
 * The SP (sub-picture) channel is opened on the SAME camera controller the app
 * already brought up for the MP (uplink encode) channel, then frames are pulled
 * with bk_isp_camera_read(). This mirrors the proven reference overlay path in
 * projects/multimedia/h264d_gpu_display_example (isp_overlay_task), which reads
 * ISP_SP_CHN_ID via bk_isp_camera_read() on the camera-controller handle.
 *
 * An earlier attempt used direct pop_buf + an MP mid-frame ISR sync (copied from
 * the single-shot software-snapshot path); that is unnecessary for a continuous
 * SP stream and only added contention. bk_isp_camera_read() on the controller
 * handle is the supported continuous-read path.
 */

#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <common/avdk_pixel_types.h>
#include <components/bk_isp_camera.h>
#include <components/bk_camera_isp_ctlr.h>
#include <components/bk_camera_configs.h>
#include <driver/isp_types.h>

#include "app_camera.h"
#include "doorbell_isp_sp.h"

#define TAG "db-isp-sp"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* When PIP is enabled right after the camera turns on, the MP channel state is
 * already ON but the sensor/ISP pipeline has not delivered its first frame yet
 * (empirically ~380ms of warmup). app_isp_camera_sp_snapshot_channel_ensure()
 * arms an MP frame-complete ISR and waits a fixed 200ms; that one-shot window
 * expires before the first MP frame, so the SP open aborts with AVDK_ERR_TIMEOUT
 * and the self-view PIP never appears. Retry the MP-synced open a bounded number
 * of times so we catch the first real MP frame once streaming begins. This runs
 * on the camera-turn-on thread (not the downlink decode task), so the extra wait
 * does not stall the remote picture. A non-timeout error (e.g. NO_RESOURCE) is
 * fatal and stops the loop immediately instead of spinning. */
#define SP_MP_SYNC_MAX_ATTEMPTS   (8U)
#define SP_MP_SYNC_RETRY_GAP_MS   (20U)

static bool s_sp_open = false;

int doorbell_isp_sp_open(uint16_t width, uint16_t height)
{
    bk_isp_camera_ctlr_handle_t camera;
    bk_isp_camera_channel_config_t instance;
    avdk_err_t ret;
    bool mp_on;

    camera = app_isp_camera_ctlr_handle_get();
    if (camera == NULL)
    {
        LOGE("%s, no camera controller handle\n", __func__);
        return BK_FAIL;
    }

    /* Already streaming (e.g. opened by a prior snapshot / prior call) -> reuse. */
    if (bk_isp_camera_channel_state_get(camera, ISP_SP_CHN_ID) == ISP_CHANNEL_STATE_TURN_ON)
    {
        s_sp_open = true;
        LOGI("%s, SP already on, reuse %ux%u\n", __func__, width, height);
        return BK_OK;
    }

    mp_on = (bk_isp_camera_channel_state_get(camera, ISP_MP_CHN_ID) == ISP_CHANNEL_STATE_TURN_ON);
    if (mp_on)
    {
        uint32_t attempt;

        /* MP flexa uplink is already streaming: open SP at an MP frame boundary
         * (same path as software snapshot / app_isp_camera_sp_snapshot_channel_ensure).
         * Retry on AVDK_ERR_TIMEOUT so we survive the MP first-frame warmup window
         * (see SP_MP_SYNC_* notes above). */
        ret = AVDK_ERR_TIMEOUT;
        for (attempt = 0U; attempt < SP_MP_SYNC_MAX_ATTEMPTS; attempt++)
        {
            ret = app_isp_camera_sp_snapshot_channel_ensure(width, height);
            if (ret == AVDK_ERR_OK)
            {
                break;
            }
            if (ret != AVDK_ERR_TIMEOUT)
            {
                /* Real failure (e.g. AVDK_ERR_NO_RESOURCE): stop, don't spin. */
                break;
            }
            rtos_delay_milliseconds(SP_MP_SYNC_RETRY_GAP_MS);
        }
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, SP MP-sync open %ux%u failed=%d after %u attempts\n",
                 __func__, width, height, (int)ret, (unsigned)attempt);
            return BK_FAIL;
        }
        s_sp_open = true;
        LOGI("%s, SP channel opened (MP-sync) %ux%u NV12 after %u attempt(s)\n",
             __func__, width, height, (unsigned)(attempt + 1U));
        return BK_OK;
    }

    instance = (bk_isp_camera_channel_config_t)CAM_MP_NV12_RB_INSTANCE_CONFIG(width, height);
    instance.port_id = 0;
    instance.enable_flexa = 0;
    instance.work_mode = 0;     /* frame mode */
    instance.buf_cnt = 2;
    instance.width = width;
    instance.height = height;
    instance.format = BK_PIXEL_FORMAT_NV12;

    ret = bk_isp_camera_channel_open(camera, ISP_SP_CHN_ID, &instance);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, SP channel open %ux%u failed=%d\n", __func__, width, height, (int)ret);
        return BK_FAIL;
    }

    s_sp_open = true;
    LOGI("%s, SP channel opened %ux%u NV12\n", __func__, width, height);
    return BK_OK;
}

int doorbell_isp_sp_read_nv12(uint8_t *frame, uint32_t size, uint32_t timeout_ms)
{
    bk_isp_camera_ctlr_handle_t camera;
    avdk_err_t ret;

    if (frame == NULL || size == 0U || !s_sp_open)
    {
        return BK_FAIL;
    }

    camera = app_isp_camera_ctlr_handle_get();
    if (camera == NULL)
    {
        return BK_FAIL;
    }

    ret = bk_isp_camera_read(camera, ISP_SP_CHN_ID, frame, size, timeout_ms);
    if (ret != AVDK_ERR_OK)
    {
        return BK_ERR_TIMEOUT;
    }
    return BK_OK;
}

void doorbell_isp_sp_close(void)
{
    bk_isp_camera_ctlr_handle_t camera;

    if (!s_sp_open)
    {
        return;
    }

    camera = app_isp_camera_ctlr_handle_get();
    if (camera != NULL)
    {
        (void)bk_isp_camera_channel_close(camera, ISP_SP_CHN_ID);
    }
    s_sp_open = false;
    LOGI("%s, SP channel closed\n", __func__);
}
