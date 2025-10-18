#ifndef __DOORBELL_VIDEO_DEVICE_H__
#define __DOORBELL_VIDEO_DEVICE_H__

#include "doorbell_devices.h"

#define CAMERA_DEVICES_REPORT (BK_FALSE)//(BK_TRUE)

/**
 * @brief      UVC camera turn on
 *
 * @param      parameters  The parameters
 *
 * @return     The status
 * BK_OK: success
 * BK_ERR: fail
 * BK_ERR_INVALID_PARAM: invalid parameter
 * BK_ERR_NOT_SUPPORT: not support
 */
int doorbell_uvc_camera_turn_on(camera_parameters_t *parameters);

/**
 * @brief      DVP camera turn on
 *
 * @param      parameters  The parameters
 *
 * @return     The status
 * BK_OK: success
 * BK_ERR: fail
 * BK_ERR_INVALID_PARAM: invalid parameter
 * BK_ERR_NOT_SUPPORT: not support
 */
int doorbell_dvp_camera_turn_on(camera_parameters_t *parameters);

/**
 * @brief      DVP camera turn off
 *
 * @return     The status
 * BK_OK: success
 * BK_ERR: fail
 * BK_ERR_NOT_SUPPORT: not support
 */
int doorbell_camera_device_turn_off(void);

/**
 * @brief      设置H264编码句柄
 * 
 * @param      handle  H264编码句柄
 * 
 * @return     int 操作结果
 * BK_OK: 成功
 * BK_ERR: 失败
 * BK_ERR_INVALID_PARAM: 无效参数
 * BK_ERR_NOT_SUPPORT: 不支持该操作
 */
int doorbell_set_h264_encode_handle(bk_video_pipeline_handle_t handle);

/**
 * @brief      开启视频传输功能
 * 
 * @return     int 操作结果
 * BK_OK: 成功
 * BK_ERR: 失败
 * BK_ERR_NOT_SUPPORT: 不支持该操作
 */
int doorbell_video_transfer_turn_on(void);

/**
 * @brief      关闭视频传输功能
 * 
 * @return     int 操作结果
 * BK_OK: 成功
 * BK_ERR: 失败
 * BK_ERR_NOT_SUPPORT: 不支持该操作
 */
int doorbell_video_transfer_turn_off(void);

/**
 * @brief      DVP camera set parameters
 *
 * @param      parameters  The parameters
 *
 * @return     The status
 * BK_OK: success
 * BK_ERR: fail
 * BK_ERR_INVALID_PARAM: invalid parameter
 * BK_ERR_NOT_SUPPORT: not support
 */
int doorbell_dvp_camera_set_parameters(camera_parameters_t *parameters);

/**
 * @brief      设置视频传输回调函数
 * 
 * @param      cb  视频传输回调函数指针
 * 
 * @return     int 操作结果
 * BK_OK: 成功
 * BK_ERR: 失败
 * BK_ERR_INVALID_PARAM: 无效参数
 * BK_ERR_NOT_SUPPORT: 不支持该操作
 */
int doorbell_devices_set_camera_transfer_callback(void *cb);

#endif