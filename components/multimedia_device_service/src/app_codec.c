#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <components/log.h>
#include <driver/int.h>
#include <common/bk_err.h>

#include "components/bk_frame_buffer.h"
#include "components/bk_encode/bk_h264_encode_ctlr.h"

#include "driver/isp.h"
#include "app_camera.h"
#include "app_codec.h"
#include "mds_img_manager.h"
#if CONFIG_H264E_STREAM_SESSION
#include "h264e_stream_session.h"
#endif
#if CONFIG_NTWK_H264_DROP_POLICY
#include "h264_backpressure_drop.h"
#endif

#define TAG "db-codec"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

bk_h264_encode_ctlr_handle_t app_codec_enc_handler = NULL;
beken_queue_t h264e_request_que = NULL;
beken_queue_t h264e_complete_que = NULL;
#define MAX_QUEUE_LEN 5
//#define DUMP_ENCODE_DATA_ENABLE

typedef enum
{
	H264E_MSG_REQUEST = 0,
	H264E_MSG_COMPLETE,
    H264E_MSG_ENC_FREE,
} h264e_msg_type_t;


typedef struct
{
    uint32_t event;
    uint32_t param;
} h264e_msg_t;

#define ENCODE_QUENE_ENABLE (0)
#if CONFIG_H264E_STREAM_SESSION
static beken_thread_t s_h264e_stream_transfer_thread = NULL;
static beken_semaphore_t s_h264e_stream_transfer_sem = NULL;
static volatile uint8_t s_h264e_stream_transfer_enable = 0;

static void h264e_stream_transfer_task_entry(beken_thread_arg_t data)
{
    (void)data;
    frame_buffer_t *frame = NULL;

    rtos_set_semaphore(&s_h264e_stream_transfer_sem);

    while (s_h264e_stream_transfer_enable) {
        frame = (frame_buffer_t *)bk_encoded_complete_data_request(50);
        if (frame == NULL) {
            continue;
        }

        (void)h264e_stream_session_send_h264((uint8_t *)frame, frame->length);
        bk_encoded_data_free_request((uint8_t *)frame);
    }

    s_h264e_stream_transfer_thread = NULL;
    rtos_set_semaphore(&s_h264e_stream_transfer_sem);
    rtos_delete_thread(NULL);
}

static bk_err_t h264e_stream_transfer_start(void)
{
    bk_err_t ret;

    if (s_h264e_stream_transfer_thread != NULL) {
        return BK_OK;
    }

    if (s_h264e_stream_transfer_sem == NULL) {
        ret = rtos_init_semaphore(&s_h264e_stream_transfer_sem, 1);
        if (ret != BK_OK) {
            LOGE("transfer sem init failed: %d\r\n", ret);
            return ret;
        }
    }

    s_h264e_stream_transfer_enable = 1;
    ret = rtos_create_hsram_thread(&s_h264e_stream_transfer_thread,
                                   BEKEN_DEFAULT_WORKER_PRIORITY,
                                   "h264e_stream_trs",
                                   (beken_thread_function_t)h264e_stream_transfer_task_entry,
                                   4096,
                                   NULL);
    if (ret != BK_OK) {
        LOGE("transfer thread create failed: %d\r\n", ret);
        s_h264e_stream_transfer_enable = 0;
        if (s_h264e_stream_transfer_sem != NULL) {
            rtos_deinit_semaphore(&s_h264e_stream_transfer_sem);
            s_h264e_stream_transfer_sem = NULL;
        }
        return ret;
    }

    rtos_get_semaphore(&s_h264e_stream_transfer_sem, BEKEN_NEVER_TIMEOUT);
    return BK_OK;
}

static void h264e_stream_transfer_stop(void)
{
    s_h264e_stream_transfer_enable = 0;

    if (s_h264e_stream_transfer_thread != NULL) {
        rtos_get_semaphore(&s_h264e_stream_transfer_sem, BEKEN_NEVER_TIMEOUT);
        s_h264e_stream_transfer_thread = NULL;
    }

    if (s_h264e_stream_transfer_sem != NULL) {
        rtos_deinit_semaphore(&s_h264e_stream_transfer_sem);
        s_h264e_stream_transfer_sem = NULL;
    }
}
#endif



void *encoder_buffer_request(uint32_t buffer_len, void *args)
{
    frame_buffer_t *temp_buffer = NULL;
    if (buffer_len > 0)
    {
        temp_buffer = (frame_buffer_t *)bk_encoded_data_request();
        if(temp_buffer == NULL) {
            return NULL;
        }
    }

    return temp_buffer != NULL ? temp_buffer->frame : NULL;
}

uint32_t encoder_buffer_complete(bk_h264_encode_outbuf_info_t *info)
{
    bk_err_t ret = BK_OK;
    if (info == NULL || info->outbuf == NULL) {
        return BK_FAIL;
    }

    uint32_t frame_size = ((sizeof(frame_buffer_t) + 63) >> 6) << 6;
    frame_buffer_t *buffer = (frame_buffer_t *)((uint8_t *)info->outbuf - frame_size);
    if (info->status == BK_OK)
    {
        buffer->length = info->length;
        buffer->h264_type = info->type;
        buffer->fmt = PIXEL_FMT_H264;
        buffer->sequence = info->sequence;
        bk_encoded_data_complete_request((uint8_t *)buffer);
#if CONFIG_NTWK_H264_DROP_POLICY
        if (ntwk_h264_backpressure_drop_consume_force_idr() && app_codec_enc_handler != NULL) {
            bk_h264_encode_force_idr(app_codec_enc_handler);
        }
#endif
    }
    else
    {
        bk_encoded_data_free_request((uint8_t *)buffer);
    }
    return ret;
}

int app_h264e_turn_off(void)
{
    if (app_codec_enc_handler == NULL) {
        return BK_OK;
    }
    
#if CONFIG_H264E_STREAM_SESSION
    h264e_stream_transfer_stop();
#endif
    // 停止调试
    bk_h264_encode_ioctl(app_codec_enc_handler, BK_H264_ENCODE_IOCTL_DEBUG_STOP, NULL);

    // 关闭编码器
    bk_h264_encode_close(app_codec_enc_handler);
    
    // 去初始化
    bk_h264_encode_deinit(app_codec_enc_handler);
    
    // 删除编码器
    bk_h264_encode_delete(app_codec_enc_handler);
    app_codec_enc_handler = NULL;

    bk_encoded_data_manager_deinit(1);
    return BK_OK;
}

int app_h264e_turn_on(void)
{
    bk_err_t ret = BK_OK;

    bk_encoded_data_manager_init();

    void *isp_handle = app_isp_handle_get();

    isp_control_t *isp_control = (isp_control_t *)isp_handle;
    uint8_t chnl_id = ISP_MP_CHN_ID;

    // 配置H.264编码器
    bk_h264_encode_hw_flexa_config_t config = {
        .width = isp_control->chn[chnl_id].chn_attr.chnFormat.width,
        .height = isp_control->chn[chnl_id].chn_attr.chnFormat.height,
        .input_format = BK_PIXEL_FORMAT_NV12,
        .gop_frame_count = 40,
        .input_flexa_cnt = 3,
        .input_buf = isp_control->chn[chnl_id].y_addr,
        .input_size = isp_control->chn[chnl_id].buf_cnt,
        .outbuf_malloc = encoder_buffer_request,
        .outbuf_malloc_args = NULL,
        .outbuf_complete = encoder_buffer_complete,
        .outbuf_complete_args = NULL,
    };

    // 创建编码器
    ret = bk_h264_encode_hw_flexa_new(&app_codec_enc_handler, &config);
    if (ret != BK_OK) {
        LOGE("Create H.264 encoder failed: %d\r\n", ret);
        return ret;
    }

    // 初始化编码器
    ret = bk_h264_encode_init(app_codec_enc_handler);
    if (ret != BK_OK) {
        LOGE("Init H.264 encoder failed: %d\r\n", ret);
        bk_h264_encode_delete(app_codec_enc_handler);
        app_codec_enc_handler = NULL;
        return ret;
    }

    // 打开编码器
    ret = bk_h264_encode_open(app_codec_enc_handler);
    if (ret != BK_OK) {
        LOGE("Open H.264 encoder failed: %d\r\n", ret);
        bk_h264_encode_deinit(app_codec_enc_handler);
        bk_h264_encode_delete(app_codec_enc_handler);
        app_codec_enc_handler = NULL;
        return ret;
    }

    bk_h264_encode_rate_ctrl_t rate_ctrl = {
        .bitrate = 1500000,
        .qp_min_i = 20,
        .qp_max_i = 51,
        .qp_min_p = 26,
        .qp_max_p = 51,
    };
    ret = bk_h264_encode_set_rate_ctrl(app_codec_enc_handler, &rate_ctrl);
    if (ret != BK_OK)
    {
        LOGE("Set H.264 encoder rate control failed: %d\r\n", ret);
        return ret;
    }
    // 启动调试 (2秒间隔)
#ifndef DUMP_ENCODE_DATA_ENABLE
    uint32_t debug_interval = 2000;
    bk_h264_encode_ioctl(app_codec_enc_handler, BK_H264_ENCODE_IOCTL_DEBUG_START, &debug_interval);
#endif
#if CONFIG_H264E_STREAM_SESSION
    ret = h264e_stream_transfer_start();
    if (ret != BK_OK) {
        LOGE("h264 stream transfer start failed: %d\r\n", ret);
        return ret;
    }
#endif
    return ret;
}

void *app_h264_encode_handle_get(void)
{
    return app_codec_enc_handler;
}
