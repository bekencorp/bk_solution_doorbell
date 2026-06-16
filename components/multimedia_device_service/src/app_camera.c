#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/int.h>
#include <common/bk_err.h>

#include "app_camera.h"

#include <components/bk_isp_camera.h>
#include <components/bk_camera_isp_ctlr.h>
#include <components/bk_camera_configs.h>
#include <avdk_check.h>

#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include "gpio_driver.h"

#include <driver/i2c.h>
#include <driver/mipi_csi.h>
#include <components/bk_camera_sensor.h>
#include <components/bk_camera_bus.h>
#include <sys_types.h>
#include <modules/pm.h>
#ifdef CONFIG_MDS_SNAPSHOT
#include "bk_snapshot_sw.h"
#endif
#define TAG "db-camera"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

typedef struct {
    isp_handle_t isp_handle;
    mipi_csi_handle_t csi_handle;
    bk_camera_sensor_handle_t sensor_handle;
    bk_camera_sensor_handle_t dvp_sensor_handle;
    bk_camera_sensor_handle_t mipi_sensor_handle;
    bk_isp_camera_ctlr_handle_t camera_ctlr_handle;
} isp_csi_cam_handle_t;

static isp_csi_cam_handle_t isp_cam_handle = {0};

static camera_board_config_t *camera_board_config = NULL;

/* Flag to indicate whether direct ISP channel read API is used.
 * If false, camera is only used by encoder/doorbell (no app_isp_camera_channel_read),
 * and we can use the original simple close path.
 * If true, we must use the safe instance-stop/destroy path to avoid MemFault.
 */
static bool s_isp_camera_read_used = false;

/**
 * @brief Vote MIPI camera AuxLDOs (1.8V iovdd + 1.2V dvdd) on/off.
 *
 * Only MIPI sensors on this board need these two rails. DVP/UVC paths must NOT
 * call this helper. This is the single owner of PM_AUXLDO_USER_CAMERA; higher
 * layers MUST NOT vote PM_AUXLDO_USER_CAMERA themselves to avoid double voting.
 */
avdk_err_t app_mipi_camera_power_enable(bool enable)
{
    int ldo_en = enable ? PM_AUXLDO_ENABLE : PM_AUXLDO_DISABLE;
    LOGI("%s, iovdd dvdd enable: %d\n", __func__, ldo_en);

    pm_auxldo_ctrl_cfg_t auxldo_cfg = {0};
    auxldo_cfg.ldo = AUXLDOS_SEL_1P8V;
    auxldo_cfg.out = PM_AUXLDO_1P8V_OUT_1P8V;
    auxldo_cfg.user = PM_AUXLDO_USER_CAMERA;
    auxldo_cfg.state = ldo_en;
    AVDK_RETURN_ON_ERROR(bk_pm_auxldo_ctrl_vote(&auxldo_cfg), TAG, "camera 1p8v ldo vote failed");

    auxldo_cfg = (pm_auxldo_ctrl_cfg_t){0};
    auxldo_cfg.ldo = AUXLDOS_SEL_1P2V;
    auxldo_cfg.out = PM_AUXLDO_1P2V_OUT_1P2V;
    auxldo_cfg.user = PM_AUXLDO_USER_CAMERA;
    auxldo_cfg.state = ldo_en;
    AVDK_RETURN_ON_ERROR(bk_pm_auxldo_ctrl_vote(&auxldo_cfg), TAG, "camera 1p2v ldo vote failed");
    rtos_delay_milliseconds(1);
    return AVDK_ERR_OK;
}

void *app_isp_handle_get(void)
{
    return isp_cam_handle.isp_handle;
}

bk_isp_camera_ctlr_handle_t app_isp_camera_ctlr_handle_get(void)
{
    return isp_cam_handle.camera_ctlr_handle;
}

int app_isp_camera_turn_off(void)
{
    LOGI("%s\n", __func__);

    avdk_err_t ret = AVDK_ERR_OK;

    if (isp_cam_handle.camera_ctlr_handle == NULL && isp_cam_handle.isp_handle == NULL)
    {
        return AVDK_ERR_OK;
    }

    /* Step 1: stop and destroy MP instance if exists.
     * This will stop ISP streaming and internal ISP read task safely.
     */
    if (isp_cam_handle.camera_ctlr_handle != NULL &&
        bk_isp_camera_channel_state_get(isp_cam_handle.camera_ctlr_handle, ISP_MP_CHN_ID) == ISP_CHANNEL_STATE_TURN_ON)
    {

        ret = bk_isp_camera_channel_close(isp_cam_handle.camera_ctlr_handle, ISP_MP_CHN_ID);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, bk_isp_camera_channel_close MP failed: %d\n", __func__, ret);
            return ret;
        }
    }

    /* Step 2: stop and destroy SP instance if exists (for completeness). */
    if (isp_cam_handle.camera_ctlr_handle != NULL &&
        bk_isp_camera_channel_state_get(isp_cam_handle.camera_ctlr_handle, ISP_SP_CHN_ID) == ISP_CHANNEL_STATE_TURN_ON)
    {
        ret = bk_isp_camera_channel_close(isp_cam_handle.camera_ctlr_handle, ISP_SP_CHN_ID);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, bk_isp_camera_channel_close SP failed: %d\n", __func__, ret);
            return ret;
        }
    }

    /* Step 3: deinit and delete camera controller. */
    if (isp_cam_handle.camera_ctlr_handle)
    {
        ret = bk_isp_camera_deinit(isp_cam_handle.camera_ctlr_handle);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, bk_isp_camera_deinit failed: %d\n", __func__, ret);
            return ret;
        }

        ret = bk_isp_camera_delete(isp_cam_handle.camera_ctlr_handle);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s, bk_isp_camera_delete failed: %d\n", __func__, ret);
            return ret;
        }

        isp_cam_handle.camera_ctlr_handle = NULL;
    }

    /* I2C bus is allocated in sensor init / DVP open (bk_camera_bus_new) and is not
     * freed by bk_isp_camera_delete; match projects/multimedia/isp_example teardown. */
    bk_camera_bus_t *bus = bk_camera_bus_get();
    if (bus != NULL)
    {
        avdk_err_t bus_ret = bk_camera_bus_disable(bus);
        if (bus_ret != AVDK_ERR_OK)
        {
            LOGW("%s, bk_camera_bus_disable failed: %d\n", __func__, bus_ret);
        }
        bus_ret = bk_camera_bus_delete(bus);
        if (bus_ret != AVDK_ERR_OK)
        {
            LOGW("%s, bk_camera_bus_delete failed: %d\n", __func__, bus_ret);
        }
    }

    isp_cam_handle.isp_handle = NULL;
    if (isp_cam_handle.sensor_handle)
    {
        bk_camera_sensor_destroy(isp_cam_handle.sensor_handle);
        isp_cam_handle.sensor_handle = NULL;
    }
    if (isp_cam_handle.dvp_sensor_handle)
    {
        bk_camera_sensor_destroy(isp_cam_handle.dvp_sensor_handle);
        isp_cam_handle.dvp_sensor_handle = NULL;
    }
    if (isp_cam_handle.mipi_sensor_handle)
    {
        bk_camera_sensor_destroy(isp_cam_handle.mipi_sensor_handle);
        isp_cam_handle.mipi_sensor_handle = NULL;
    }
    AVDK_RETURN_ON_ERROR(app_mipi_camera_power_enable(false), TAG, "app_mipi_camera_power_enable disable");
    return AVDK_ERR_OK;
}

int app_isp_dvp_camera_turn_on(camera_parameters_ext_t *paramters)
{
    bk_err_t ret = BK_OK;

    AVDK_GOTO_ON_FALSE(paramters, AVDK_ERR_INVAL, err, TAG, "cparamters is null");

    bk_isp_camera_ctlr_config_t isp_ctlr_config = CAM_DVP_DEFAULT_RAW8_CONFIG(paramters->camera_width, paramters->camera_height, paramters->fps);
    bk_camera_bus_t *bus = NULL;
    bk_camera_bus_config_t bus_config = DVP_CAM_BUS_I2C1_8BIT_2000TIMEOUT();
    bus_config.pin_scl = camera_board_config->mipi.pin_scl;
    bus_config.pin_sda = camera_board_config->mipi.pin_sda;
    bus_config.i2c_id = camera_board_config->mipi.i2c_id;

    bk_camera_sensor_config_t sensor_config = {
        .pin_reset = camera_board_config->mipi.pin_reset,
        .pin_pwdn = camera_board_config->mipi.pin_pwdn,
        .pin_xclk = camera_board_config->mipi.pin_xclk,
    };

    if (paramters->camera_width > camera_board_config->mipi.sensor_max_width || paramters->camera_width == 0)
    {
        paramters->camera_width = camera_board_config->mipi.sensor_max_width;
    }

    if (paramters->camera_height > camera_board_config->mipi.sensor_max_height || paramters->camera_height == 0)
    {
        paramters->camera_height = camera_board_config->mipi.sensor_max_height;
    }

    if (paramters->fps > camera_board_config->mipi.sensor_fps || paramters->fps == 0)
    {
        paramters->fps = camera_board_config->mipi.sensor_fps;
    }

    AVDK_RETURN_ON_ERROR(app_mipi_camera_power_enable(true), TAG, "app_mipi_camera_power_enable enable");
    LOGI("%s width: %d, height: %d, fps: %d\n", __func__, paramters->camera_width, paramters->camera_height, paramters->fps);

    //step 1. create/enable bus
    bus = bk_camera_bus_new(&bus_config);
    AVDK_GOTO_ON_FALSE(bus, AVDK_ERR_GENERIC, err, TAG, "camera bus new failed");
    AVDK_GOTO_ON_ERROR(bk_camera_bus_enable(bus), err, TAG, "camera bus enable failed");

    //step 2. sensor detect
    sensor_config.bus = bus;
    isp_cam_handle.sensor_handle = bk_camera_sensor_auto_detect(&sensor_config, DVP_CAMERA_PORT);
    AVDK_RETURN_ON_FALSE(isp_cam_handle.sensor_handle, ret, TAG, "sensor handle is NULL");

    const void *sensor_object = bk_camera_sensor_get_sensor_object(isp_cam_handle.sensor_handle);
    AVDK_RETURN_ON_FALSE(sensor_object, ret, TAG, "sensor object is NULL");
    isp_ctlr_config.sensor_object = sensor_object;

    /* Pull sensor's native output pixel format from its supported-format table
     * (declared per-device in xxx_format_array[]); the macro no longer sets it.
     * Prefer the entry matching the requested (w,h,fps); fall back to [0]. */
    {
        bk_camera_sensor_format_array_t format_array = {0};
        if (bk_camera_sensor_query_support_formats(isp_cam_handle.sensor_handle, &format_array) == AVDK_ERR_OK
            && format_array.size > 0)
        {
            uint32_t i;
            isp_ctlr_config.input_pixel_fmt = format_array.format_array[0].output_pixel_fmt;
            for (i = 0; i < format_array.size; i++)
            {
                if (format_array.format_array[i].width  == paramters->camera_width
                    && format_array.format_array[i].height == paramters->camera_height
                    && format_array.format_array[i].fps    == paramters->fps)
                {
                    isp_ctlr_config.input_pixel_fmt = format_array.format_array[i].output_pixel_fmt;
                    break;
                }
            }
        }
    }

    // step 3: new/init camera
    AVDK_RETURN_ON_ERROR(bk_camera_isp_ctlr_new(&isp_cam_handle.camera_ctlr_handle), TAG, "bk_camera_isp_ctlr_new failed");
    bk_camera_isp_ctlr_t *control =  __containerof(isp_cam_handle.camera_ctlr_handle, bk_camera_isp_ctlr_t, ops);
    AVDK_RETURN_ON_FALSE(control, ret, TAG, "control is NULL");

    AVDK_GOTO_ON_ERROR(bk_isp_camera_dev_init(isp_cam_handle.camera_ctlr_handle), err, TAG, "bk_isp_camera_dev_init failed");
    isp_cam_handle.isp_handle = control->isp_handle;

    // step 3.1: init camera port mipi/dvp
    AVDK_GOTO_ON_ERROR(bk_isp_camera_port_init(isp_cam_handle.camera_ctlr_handle, &isp_ctlr_config), err, TAG, "bk_dvp_port_init failed");

    // step 4: enable isp mp channel
    uint16_t isp_output_width = (paramters->isp_output_width != 0) ? paramters->isp_output_width : paramters->camera_width;
    uint16_t isp_output_height = (paramters->isp_output_height != 0) ? paramters->isp_output_height : paramters->camera_height;
    LOGI("ISP output: %dx%d (camera input: %dx%d)\n", isp_output_width, isp_output_height, paramters->camera_width, paramters->camera_height);

    bk_isp_camera_channel_config_t instance = CAM_MP_NV12_RB_INSTANCE_CONFIG(isp_output_width, isp_output_height);
    instance.port_id = 1;//TODO FIXME

    ret = bk_isp_camera_channel_open(isp_cam_handle.camera_ctlr_handle, ISP_MP_CHN_ID, &instance);

    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d, bk_isp_camera_open failed ret: %d\n", __func__, __LINE__, ret);
    }

    // step 5: enable sensor
    bk_camera_sensor_init(isp_cam_handle.sensor_handle);
    bk_camera_sensor_format_t format = {
        .width = paramters->camera_width,
        .height = paramters->camera_height,
        .fps = paramters->fps,
    };
    bk_camera_sensor_set_format(isp_cam_handle.sensor_handle, &format);

    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d, bk_isp_camera_open failed ret: %d\n", __func__, __LINE__, ret);
    }

    return AVDK_ERR_OK;

err:
    if (isp_cam_handle.camera_ctlr_handle)
    {
        //TODO
    }

    return AVDK_ERR_GENERIC;
}

int app_isp_mipi_sensor_init(const camera_board_config_t *config, bk_isp_camera_ctlr_config_t *isp_ctlr_config)
{
    bk_err_t ret = BK_OK;
    bk_camera_bus_t *bus = NULL;

    AVDK_GOTO_ON_FALSE(config, AVDK_ERR_INVAL, err, TAG, "config is null\n");

    bk_camera_bus_config_t bus_config = CSI_CAM_BUS_I2C1_8BIT_2000TIMEOUT();
    bus_config.pin_scl = camera_board_config->mipi.pin_scl;
    bus_config.pin_sda = camera_board_config->mipi.pin_sda;
    bus_config.i2c_id = camera_board_config->mipi.i2c_id;

    bk_camera_sensor_config_t sensor_config = {
        .pin_reset = camera_board_config->mipi.pin_reset,
        .pin_pwdn = camera_board_config->mipi.pin_pwdn,
        .pin_xclk = camera_board_config->mipi.pin_xclk,
    };

    LOGI("%s width: %d, height: %d, fps: %d\n", __func__, config->mipi.sensor_max_width, config->mipi.sensor_max_height, config->mipi.sensor_fps);

    //step 1. create/enable bus
    bus = bk_camera_bus_new(&bus_config);
    AVDK_GOTO_ON_FALSE(bus, AVDK_ERR_GENERIC, err, TAG, "camera bus new failed");
    AVDK_GOTO_ON_ERROR(bk_camera_bus_enable(bus), err, TAG, "camera bus enable failed");

    //step 2. sensor detect
    sensor_config.bus = bus;
    isp_cam_handle.sensor_handle = bk_camera_sensor_auto_detect(&sensor_config, CSI_CAMERA_PORT);
    AVDK_GOTO_ON_FALSE(isp_cam_handle.sensor_handle, AVDK_ERR_NODEV, err, TAG, "sensor handle is NULL");

    bk_camera_sensor_format_array_t format_array = {0};
    AVDK_GOTO_ON_ERROR(bk_camera_sensor_query_support_formats(isp_cam_handle.sensor_handle, &format_array), err, TAG, "bk_camera_sensor_query_support_formats failed");
    AVDK_GOTO_ON_FALSE(format_array.size > 0, AVDK_ERR_INVAL, err, TAG, "format array size is 0");

    int detect_index = 0;
    for (detect_index = 0; detect_index < format_array.size; detect_index++)
    {
        if (format_array.format_array[detect_index].width == config->mipi.sensor_max_width
            && format_array.format_array[detect_index].height == config->mipi.sensor_max_height
            && format_array.format_array[detect_index].fps == config->mipi.sensor_fps)
        {
            break;
        }
    }

    if (detect_index >= format_array.size)
    {
        LOGE("not found format_array\n");
        ret = AVDK_ERR_INVAL;
        goto err;
    }

    LOGI("found format_array[%d]: %d, %d, %d\n", detect_index,
        format_array.format_array[detect_index].width,
        format_array.format_array[detect_index].height,
        format_array.format_array[detect_index].fps);

    /* Pixel format is a property of the matched sensor mode; copy it back into
     * isp_ctlr_config so the ISP port is configured for the correct raw type. */
    isp_ctlr_config->input_pixel_fmt = format_array.format_array[detect_index].output_pixel_fmt;

    const void *sensor_object = bk_camera_sensor_get_sensor_object(isp_cam_handle.sensor_handle);
    AVDK_GOTO_ON_FALSE(sensor_object, AVDK_ERR_GENERIC, err, TAG, "sensor object is NULL");
    isp_ctlr_config->sensor_object = sensor_object;

    return AVDK_ERR_OK;

err:
    /* Cleanup on failure: destroy sensor handle and bus to avoid stale state. */
    if (isp_cam_handle.sensor_handle)
    {
        bk_camera_sensor_destroy(isp_cam_handle.sensor_handle);
        isp_cam_handle.sensor_handle = NULL;
    }
    if (bus)
    {
        bk_camera_bus_disable(bus);
        bk_camera_bus_delete(bus);
    }
    return (ret != BK_OK) ? ret : AVDK_ERR_GENERIC;
}

int app_isp_mipi_sensor_start(const camera_board_config_t *config)
{
    // step 5: enable sensor
    bk_camera_sensor_init(isp_cam_handle.sensor_handle);
    bk_camera_sensor_format_t format = {
        .width = config->mipi.sensor_max_width,
        .height = config->mipi.sensor_max_height,
        .fps = config->mipi.sensor_fps,
    };

    return bk_camera_sensor_set_format(isp_cam_handle.sensor_handle, &format);
}

int app_isp_mipi_camera_mp_turn_on(const camera_board_config_t *config, bk_isp_camera_ctlr_config_t *isp_ctlr_config)
{
    bk_err_t ret = BK_OK;
    AVDK_GOTO_ON_FALSE(config, AVDK_ERR_INVAL, err, TAG, "config is null");

    // step 3: new/init camera
    AVDK_RETURN_ON_ERROR(bk_camera_isp_ctlr_new(&isp_cam_handle.camera_ctlr_handle), TAG, "bk_camera_isp_ctlr_new failed");
    bk_camera_isp_ctlr_t *control =  __containerof(isp_cam_handle.camera_ctlr_handle, bk_camera_isp_ctlr_t, ops);
    AVDK_RETURN_ON_FALSE(control, ret, TAG, "control is NULL");

    AVDK_GOTO_ON_ERROR(bk_isp_camera_dev_init(isp_cam_handle.camera_ctlr_handle), err, TAG, "bk_isp_camera_dev_init failed");
    isp_cam_handle.isp_handle = control->isp_handle;

    // step 3.1: init camera port mipi/dvp
    AVDK_GOTO_ON_ERROR(bk_isp_camera_port_init(isp_cam_handle.camera_ctlr_handle, isp_ctlr_config), err, TAG, "bk_mipi_port_init failed");

    // step 4: enable isp mp channel
    // Use ISP output width/height if provided, otherwise use camera input width/height
    LOGI("ISP output: %dx%d (camera input: %dx%d)\n", config->isp.mp_width, config->isp.mp_height, config->mipi.sensor_max_width, config->mipi.sensor_max_height);

    bk_isp_camera_channel_config_t instance = CAM_MP_NV12_RB_INSTANCE_CONFIG(config->isp.mp_width, config->isp.mp_height);

    instance.port_id = ISP_MP_CHN_ID;
    instance.enable_flexa = config->isp.mp_flexa ? 1 : 0;
    instance.work_mode = config->isp.mp_flexa ? 1 : 0;
    instance.format = config->isp.mp_format;

    ret = bk_isp_camera_channel_open(isp_cam_handle.camera_ctlr_handle, ISP_MP_CHN_ID, &instance);

    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d, bk_isp_camera_channel_open failed ret: %d\n", __func__, __LINE__, ret);
    }

    return AVDK_ERR_OK;

err:
    return AVDK_ERR_GENERIC;
}

int app_isp_mipi_camera_turn_on(const camera_board_config_t *config)
{
    bk_err_t ret = BK_OK;
    bool ldo_voted = false;

    /* Validate state and arguments before voting LDO so a duplicate/invalid call
     * does not leave a dangling LDO vote. */
    AVDK_RETURN_ON_FALSE((isp_cam_handle.camera_ctlr_handle == NULL), AVDK_ERR_BUSY, TAG, "camera already turned on");
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is null");

    AVDK_RETURN_ON_ERROR(app_mipi_camera_power_enable(true), TAG, "app_mipi_camera_power_enable failed");
    ldo_voted = true;

    bk_isp_camera_ctlr_config_t isp_ctlr_config = CAM_CSI_DEFAULT_RAW10_CONFIG(config->mipi.sensor_max_width, config->mipi.sensor_max_height, config->mipi.sensor_fps);

    ret = app_isp_mipi_sensor_init(config, &isp_ctlr_config);
    AVDK_GOTO_ON_FALSE(ret == AVDK_ERR_OK, ret, err, TAG, "app_isp_mipi_sensor_turn_on failed");
    ret = app_isp_mipi_camera_mp_turn_on(config, &isp_ctlr_config);
    AVDK_GOTO_ON_FALSE(ret == AVDK_ERR_OK, ret, err, TAG, "app_isp_mipi_camera_mp_turn_on failed");
    return app_isp_mipi_sensor_start(config);

err:
    if (ldo_voted)
    {
        (void)app_mipi_camera_power_enable(false);
    }
    return (ret != BK_OK) ? ret : AVDK_ERR_GENERIC;
}

int app_isp_dual_camera_turn_on(camera_parameters_ext_t *paramters)
{
    bk_err_t ret = BK_OK;
    bk_camera_bus_t *bus = NULL;
    bool ldo_voted = false;

    AVDK_RETURN_ON_FALSE(paramters, AVDK_ERR_INVAL, TAG, "cparamters is null");
    camera_parameters_ext_t param_temp = {
        .camera_width = 1920,
        .camera_height = 1080,
        .fps = 20,
    };
    AVDK_RETURN_ON_ERROR(app_mipi_camera_power_enable(true), TAG, "app_mipi_camera_power_enable failed");
    ldo_voted = true;
    bk_isp_camera_ctlr_config_t isp_ctlr_dvp_config = CAM_DVP_DEFAULT_RAW8_CONFIG(paramters->camera_width, paramters->camera_height, paramters->fps);
    bk_isp_camera_ctlr_config_t isp_ctlr_mipi_config = CAM_CSI_DEFAULT_RAW10_CONFIG(param_temp.camera_width, param_temp.camera_height, paramters->fps);

    bk_camera_bus_config_t bus_config = DUAL_CAM_BUS_I2C1_8BIT_2000TIMEOUT();
    bus_config.pin_scl = camera_board_config->mipi.pin_scl;
    bus_config.pin_sda = camera_board_config->mipi.pin_sda;
    bus_config.i2c_id = camera_board_config->mipi.i2c_id;

    bk_camera_sensor_config_t sensor_config = {
        .pin_reset = camera_board_config->mipi.pin_reset,
        .pin_pwdn = camera_board_config->mipi.pin_pwdn,
        .pin_xclk = camera_board_config->mipi.pin_xclk,
    };

    if (paramters->camera_width > camera_board_config->mipi.sensor_max_width || paramters->camera_width == 0)
    {
        paramters->camera_width = camera_board_config->mipi.sensor_max_width;
    }

    if (paramters->camera_height > camera_board_config->mipi.sensor_max_height || paramters->camera_height == 0)
    {
        paramters->camera_height = camera_board_config->mipi.sensor_max_height;
    }

    if (paramters->fps > camera_board_config->mipi.sensor_fps || paramters->fps == 0)
    {
        paramters->fps = camera_board_config->mipi.sensor_fps;
    }

    LOGI("%s width: %d, height: %d, fps: %d\n", __func__, paramters->camera_width, paramters->camera_height, paramters->fps);

    //step 1. create/enable dvp/mipi bus
    bus = bk_camera_bus_new(&bus_config);
    AVDK_GOTO_ON_FALSE(bus, AVDK_ERR_GENERIC, err, TAG, "camera bus new failed");
    AVDK_GOTO_ON_ERROR(bk_camera_bus_enable(bus), err, TAG, "camera bus enable failed");

    //step 2. sensor detect
    sensor_config.bus = bus;
    isp_cam_handle.dvp_sensor_handle = bk_camera_sensor_auto_detect(&sensor_config, DVP_CAMERA_PORT);
    AVDK_GOTO_ON_FALSE(isp_cam_handle.dvp_sensor_handle, AVDK_ERR_NODEV, err, TAG, "dvp sensor handle is NULL");

    isp_cam_handle.mipi_sensor_handle = bk_camera_sensor_auto_detect(&sensor_config, CSI_CAMERA_PORT);
    AVDK_GOTO_ON_FALSE(isp_cam_handle.mipi_sensor_handle, AVDK_ERR_NODEV, err, TAG, "mipi sensor handle is NULL");

    const void *dvp_sensor_object = bk_camera_sensor_get_sensor_object(isp_cam_handle.dvp_sensor_handle);
    AVDK_GOTO_ON_FALSE(dvp_sensor_object, AVDK_ERR_GENERIC, err, TAG, "dvp sensor object is NULL");
    const void *mipi_sensor_object = bk_camera_sensor_get_sensor_object(isp_cam_handle.mipi_sensor_handle);
    AVDK_GOTO_ON_FALSE(mipi_sensor_object, AVDK_ERR_GENERIC, err, TAG, "mipi sensor object is NULL");

    isp_ctlr_dvp_config.sensor_object = dvp_sensor_object;
    isp_ctlr_mipi_config.sensor_object = mipi_sensor_object;

    /* Pull each sensor's native output pixel format from its supported-format
     * table; prefer the entry matching the requested (w,h,fps), fall back to
     * [0] if no exact mode is declared. */
    {
        bk_camera_sensor_format_array_t dvp_fmt_arr = {0};
        bk_camera_sensor_format_array_t mipi_fmt_arr = {0};
        uint32_t i;
        if (bk_camera_sensor_query_support_formats(isp_cam_handle.dvp_sensor_handle, &dvp_fmt_arr) == AVDK_ERR_OK
            && dvp_fmt_arr.size > 0)
        {
            isp_ctlr_dvp_config.input_pixel_fmt = dvp_fmt_arr.format_array[0].output_pixel_fmt;
            for (i = 0; i < dvp_fmt_arr.size; i++)
            {
                if (dvp_fmt_arr.format_array[i].width  == paramters->camera_width
                    && dvp_fmt_arr.format_array[i].height == paramters->camera_height
                    && dvp_fmt_arr.format_array[i].fps    == paramters->fps)
                {
                    isp_ctlr_dvp_config.input_pixel_fmt = dvp_fmt_arr.format_array[i].output_pixel_fmt;
                    break;
                }
            }
        }
        if (bk_camera_sensor_query_support_formats(isp_cam_handle.mipi_sensor_handle, &mipi_fmt_arr) == AVDK_ERR_OK
            && mipi_fmt_arr.size > 0)
        {
            isp_ctlr_mipi_config.input_pixel_fmt = mipi_fmt_arr.format_array[0].output_pixel_fmt;
            for (i = 0; i < mipi_fmt_arr.size; i++)
            {
                if (mipi_fmt_arr.format_array[i].width  == param_temp.camera_width
                    && mipi_fmt_arr.format_array[i].height == param_temp.camera_height
                    && mipi_fmt_arr.format_array[i].fps    == paramters->fps)
                {
                    isp_ctlr_mipi_config.input_pixel_fmt = mipi_fmt_arr.format_array[i].output_pixel_fmt;
                    break;
                }
            }
        }
    }

    // step 3: new/init camera device
    AVDK_GOTO_ON_ERROR(bk_camera_isp_ctlr_new(&isp_cam_handle.camera_ctlr_handle), err, TAG, "bk_camera_isp_ctlr_new failed");
    bk_camera_isp_ctlr_t *control =  __containerof(isp_cam_handle.camera_ctlr_handle, bk_camera_isp_ctlr_t, ops);
    AVDK_GOTO_ON_FALSE(control, AVDK_ERR_GENERIC, err, TAG, "control is NULL");

    AVDK_GOTO_ON_ERROR(bk_isp_camera_dev_init(isp_cam_handle.camera_ctlr_handle), err, TAG, "bk_isp_camera_dev_init failed");
    isp_cam_handle.isp_handle = control->isp_handle;

    // step 3.1: init camera port mipi/dvp
    AVDK_GOTO_ON_ERROR(bk_isp_camera_port_init(isp_cam_handle.camera_ctlr_handle, &isp_ctlr_dvp_config), err, TAG, "bk_dvp_port_init failed");
    AVDK_GOTO_ON_ERROR(bk_isp_camera_port_init(isp_cam_handle.camera_ctlr_handle, &isp_ctlr_mipi_config), err, TAG, "bk_mipi_port_init failed");

    // step 4: enable isp mp channel
    uint16_t isp_output_width = (paramters->isp_output_width != 0) ? paramters->isp_output_width : paramters->camera_width;
    uint16_t isp_output_height = (paramters->isp_output_height != 0) ? paramters->isp_output_height : paramters->camera_height;
    LOGI("ISP output: %dx%d (camera input: %dx%d)\n", isp_output_width, isp_output_height, paramters->camera_width, paramters->camera_height);

    bk_isp_camera_channel_config_t instance = CAM_MP_NV12_RB_INSTANCE_CONFIG(isp_output_width, isp_output_height);
    instance.port_id = 0;//TODO FIXME

    ret = bk_isp_camera_channel_open(isp_cam_handle.camera_ctlr_handle, ISP_MP_CHN_ID, &instance);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d, bk_isp_camera_channel_open failed ret: %d\n", __func__, __LINE__, ret);
    }

    // step 5: enable sensor
    bk_camera_sensor_format_t dvp_format = {
        .width = paramters->camera_width,
        .height = paramters->camera_height,
        .fps = paramters->fps,
    };
    bk_camera_sensor_format_t mipi_format = {
        .width = param_temp.camera_width,
        .height = param_temp.camera_height,
        .fps = param_temp.fps,
    };
    bk_camera_sensor_init(isp_cam_handle.dvp_sensor_handle);
    bk_camera_sensor_set_format(isp_cam_handle.dvp_sensor_handle, &dvp_format);
    bk_camera_sensor_init(isp_cam_handle.mipi_sensor_handle);
    bk_camera_sensor_set_format(isp_cam_handle.mipi_sensor_handle, &mipi_format);

    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d, bk_isp_camera_open failed ret: %d\n", __func__, __LINE__, ret);
    }

    return AVDK_ERR_OK;

err:
    if (ldo_voted)
    {
        (void)app_mipi_camera_power_enable(false);
    }
    return (ret != BK_OK) ? ret : AVDK_ERR_GENERIC;
}

int app_isp_dual_camera_port_change()
{
    return bk_isp_camera_port_change(isp_cam_handle.camera_ctlr_handle);
}

int app_isp_dual_dvp_on(camera_parameters_ext_t *paramters)
{
    bk_camera_sensor_format_t dvp_format = {
        .width = paramters->camera_width,
        .height = paramters->camera_height,
        .fps = paramters->fps,
    };
    AVDK_RETURN_ON_ERROR(app_mipi_camera_power_enable(true), TAG, "app_mipi_camera_power_enable failed");
    bk_camera_sensor_init(isp_cam_handle.dvp_sensor_handle);
    bk_camera_sensor_set_format(isp_cam_handle.dvp_sensor_handle, &dvp_format);
    return AVDK_ERR_OK;
}

int app_isp_camera_sp_channel_turn_on(const camera_board_config_t *config)
{
    bk_err_t ret = BK_OK;
    AVDK_GOTO_ON_FALSE(config, AVDK_ERR_INVAL, err, TAG, "config is null");

    LOGI("ISP sp channel output: %dx%d\n", config->isp.sp_width, config->isp.sp_height);

    bk_isp_camera_channel_config_t instance = CAM_MP_NV12_RB_INSTANCE_CONFIG(config->isp.sp_width, config->isp.sp_height);
    instance.port_id = 0;//TODO FIXME
    instance.enable_flexa = 0;
    instance.work_mode = 0;
    instance.buf_cnt = 2;
    instance.width = config->isp.sp_width;
    instance.height = config->isp.sp_height;
    instance.format = config->isp.sp_format;

    ret = bk_isp_camera_channel_open(isp_cam_handle.camera_ctlr_handle, ISP_SP_CHN_ID, &instance);

    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, %d, bk_isp_camera_channel_open failed ret: %d\n", __func__, __LINE__, ret);
    }

    return AVDK_ERR_OK;

err:
    return AVDK_ERR_GENERIC;
}

avdk_err_t app_isp_camera_sp_snapshot_channel_ensure(uint16_t width, uint16_t height)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (width == 0 || height == 0)
    {
        return AVDK_ERR_INVAL;
    }

    if (isp_cam_handle.camera_ctlr_handle == NULL)
    {
        return AVDK_ERR_NODEV;
    }

    if (bk_isp_camera_channel_state_get(isp_cam_handle.camera_ctlr_handle, ISP_SP_CHN_ID) ==
        ISP_CHANNEL_STATE_TURN_ON)
    {
        return AVDK_ERR_OK;
    }

#ifdef CONFIG_MDS_SNAPSHOT
    ret = bk_snapshot_sw_sp_channel_open_at_mp_mid_frame(isp_cam_handle.camera_ctlr_handle, width, height);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, open SP %ux%u at MP mid-frame interrupt failed ret=%d\n", __func__, width, height, ret);
        return ret;
    }
#else
    bk_isp_camera_channel_config_t instance = CAM_MP_NV12_RB_INSTANCE_CONFIG(width, height);
    instance.port_id = 0;
    instance.enable_flexa = 0;
    instance.work_mode = 0;
    instance.buf_cnt = 2;
    instance.width = width;
    instance.height = height;
    instance.format = BK_PIXEL_FORMAT_NV12;

    ret = bk_isp_camera_channel_open(isp_cam_handle.camera_ctlr_handle, ISP_SP_CHN_ID, &instance);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s, open SP %ux%u failed ret=%d\n", __func__, width, height, ret);
        return ret;
    }
#endif

    LOGI("%s, SP snapshot channel ready %ux%u\n", __func__, width, height);
    return AVDK_ERR_OK;
}

int app_isp_camera_channel_read(uint8_t channel ,uint8_t *frame, uint32_t size, uint32_t timeout)
{
    /* Mark that direct ISP channel read is used in this project.
     * This will switch app_isp_camera_turn_off() to the safe close path.
     */
    s_isp_camera_read_used = true;

    if (channel == APP_ISP_MP_CHN_ID)
    {
        return bk_isp_camera_read(isp_cam_handle.camera_ctlr_handle, ISP_MP_CHN_ID, frame, size, timeout);
    }
    else if (channel == APP_ISP_SP_CHN_ID)
    {
        return bk_isp_camera_read(isp_cam_handle.camera_ctlr_handle, ISP_SP_CHN_ID, frame, size, timeout);
    }
    else
    {
        LOGE("%s, %d, invalid channel: %d\n", __func__, __LINE__, channel);
        return AVDK_ERR_INVAL;
    }

    return AVDK_ERR_OK;
}

int app_isp_camera_soft_reset(void)
{
    return bk_isp_camera_ctlr_ioctl(isp_cam_handle.camera_ctlr_handle, BK_CAM_IOCTL_SOFTRESET, NULL);
}

bool app_isp_camera_state_get(void)
{
    return 0;
}

int app_camera_board_config_set(camera_board_config_t *config)
{
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");
    if (camera_board_config == NULL)
    {
        camera_board_config = os_malloc(sizeof(camera_board_config_t));
        AVDK_RETURN_ON_FALSE(camera_board_config, AVDK_ERR_GENERIC, TAG, "camera_board_config malloc failed");
    }

    os_memcpy(camera_board_config, config, sizeof(camera_board_config_t));

    return AVDK_ERR_OK;
}

camera_board_config_t *app_camera_board_config_get(void)
{
    return camera_board_config;
}