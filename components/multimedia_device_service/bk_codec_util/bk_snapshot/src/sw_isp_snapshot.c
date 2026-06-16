// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <components/bk_camera_configs.h>
#include <components/bk_isp_camera.h>
#include <components/bk_camera_isp_ctlr.h>
#include <components/bk_encode/bk_jpeg_encode_ctlr.h>
#include <driver/isp_types.h>
#include "bk_snapshot_sw.h"
#include <common/avdk_pixel_types.h>
#include <driver/isp_base.h>
#include <components/bk_frame_buffer.h>

#define TAG "bk_snap_sw"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BK_SNAPSHOT_SW_MAX_FRAME_BYTES   ((BK_SNAPSHOT_SW_DEFAULT_WIDTH * BK_SNAPSHOT_SW_DEFAULT_HEIGHT * 3U) / 2U)
#define BK_SNAPSHOT_SW_JPEG_OUTBUF_SIZE  (256U * 1024U)
/* Drop warmup frames after SP open; capture the next stable frame. */
#define BK_SNAPSHOT_SW_SP_WARMUP_DROP_CNT  (1U)
#define BK_SNAPSHOT_SW_MP_FRAME_WAIT_MS    (200U)

static void bk_snapshot_sw_jpeg_buf_free(void *data)
{
	if (data != NULL) {
		bk_frame_buffer_free(data);
	}
}

typedef struct {
	beken_mutex_t mutex;
	bool mutex_inited;
	bool sp_channel_open;
	bool sp_needs_warmup;
	uint16_t sp_width;
	uint16_t sp_height;
	bk_isp_camera_ctlr_handle_t sp_camera;
	uint8_t *nv12_buf;
	bk_jpeg_encode_ctlr_handle_t jpege_handle;
	bool jpege_open;
	uint16_t jpege_width;
	uint16_t jpege_height;
} bk_snapshot_sw_module_ctx_t;

static bk_snapshot_sw_module_ctx_t s_sw_snapshot = {0};

typedef struct {
	uint8_t *jpeg_data;
	uint32_t jpeg_size;
	uint32_t enc_status;
} bk_snapshot_sw_enc_ctx_t;

static bk_snapshot_sw_enc_ctx_t s_sw_enc_ctx;

typedef struct {
	beken_semaphore_t sem;
	bool sem_inited;
	bool waiting;
	bool mid_frame_hit;
	bk_isp_camera_ctlr_handle_t camera;
	uint16_t sp_width;
	uint16_t sp_height;
	uint32_t trigger_line;
} bk_snapshot_sw_mp_sync_t;

static bk_snapshot_sw_mp_sync_t s_mp_frame_sync = {0};

static avdk_err_t bk_snapshot_sw_buffers_ensure(void)
{
	if (s_sw_snapshot.nv12_buf == NULL) {
		s_sw_snapshot.nv12_buf = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, BK_SNAPSHOT_SW_MAX_FRAME_BYTES);
		if (s_sw_snapshot.nv12_buf == NULL) {
			LOGE("alloc nv12 buf failed, need %u bytes\r\n", BK_SNAPSHOT_SW_MAX_FRAME_BYTES);
			return AVDK_ERR_NOMEM;
		}
	}

	return AVDK_ERR_OK;
}

static void bk_snapshot_sw_nv12_buf_free(void)
{
	if (s_sw_snapshot.nv12_buf != NULL) {
		bk_frame_buffer_free(s_sw_snapshot.nv12_buf);
		s_sw_snapshot.nv12_buf = NULL;
	}
}

static void bk_snapshot_sw_default_free(void *data, void *user_data)
{
	(void)user_data;
	bk_snapshot_sw_jpeg_buf_free(data);
}

static avdk_err_t bk_snapshot_sw_mutex_lock(void)
{
	if (!s_sw_snapshot.mutex_inited) {
		if (rtos_init_mutex(&s_sw_snapshot.mutex) != BK_OK) {
			return AVDK_ERR_GENERIC;
		}
		s_sw_snapshot.mutex_inited = true;
	}

	rtos_lock_mutex(&s_sw_snapshot.mutex);
	return AVDK_ERR_OK;
}

static void bk_snapshot_sw_mutex_unlock(void)
{
	if (s_sw_snapshot.mutex_inited) {
		rtos_unlock_mutex(&s_sw_snapshot.mutex);
	}
}

static uint8_t bk_snapshot_sw_map_jpege_quality(uint8_t quality)
{
	if (quality == 0) {
		quality = BK_SNAPSHOT_SW_DEFAULT_QUALITY;
	}
	if (quality <= 10) {
		return quality;
	}
	return (uint8_t)(quality / 10U);
}

static void bk_snapshot_sw_sp_fill_instance(bk_isp_camera_channel_config_t *instance,
					    uint16_t width,
					    uint16_t height)
{
	*instance = (bk_isp_camera_channel_config_t)CAM_MP_NV12_RB_INSTANCE_CONFIG(width, height);
	instance->port_id = 0;
	instance->enable_flexa = 0;
	instance->work_mode = 0;
	instance->buf_cnt = 2;
	instance->width = width;
	instance->height = height;
	instance->format = BK_PIXEL_FORMAT_NV12;
}

static bool bk_snapshot_sw_mp_channel_ready(bk_isp_camera_ctlr_handle_t camera)
{
	bk_camera_isp_ctlr_t *control;
	isp_control_t *isp_control;

	if (camera == NULL) {
		return false;
	}

	control = __containerof(camera, bk_camera_isp_ctlr_t, ops);
	isp_control = (isp_control_t *)control->isp_handle;
	return (isp_control != NULL && isp_control->chn[ISP_MP_CHN_ID].enable);
}

static uint32_t bk_snapshot_sw_mp_mid_trigger_line(bk_isp_camera_ctlr_handle_t camera)
{
	bk_camera_isp_ctlr_t *control;
	isp_control_t *isp_control;
	uint32_t height;
	uint32_t total_lines;

	if (camera == NULL) {
		return 1;
	}

	control = __containerof(camera, bk_camera_isp_ctlr_t, ops);
	isp_control = (isp_control_t *)control->isp_handle;
	if (isp_control == NULL) {
		return 1;
	}

	height = isp_control->chn[ISP_MP_CHN_ID].chn_attr.chnFormat.height;
	if (height == 0) {
		return 1;
	}

	total_lines = (height + 15U) / 16U;
	return (total_lines > 1U) ? ((total_lines + 1U) / 2U) : 1U;
}

static void bk_snapshot_sw_mp_mid_frame_cb(uint32_t seq, uint32_t line, uint8_t chnl, uint8_t ok, void *arg)
{
	(void)seq;
	(void)arg;

	if (chnl != ISP_MP_CHN_ID || !ok) {
		return;
	}

	if (line < s_mp_frame_sync.trigger_line) {
		return;
	}

	if (s_mp_frame_sync.waiting && !s_mp_frame_sync.mid_frame_hit) {
		s_mp_frame_sync.mid_frame_hit = true;
		rtos_set_semaphore(&s_mp_frame_sync.sem);
	}
}

static avdk_err_t bk_snapshot_sw_sync_at_mp_mid_frame(bk_isp_camera_ctlr_handle_t camera,
						      bool open_sp,
						      uint16_t width,
						      uint16_t height,
						      avdk_err_t *open_ret)
{
	avdk_err_t ret = AVDK_ERR_OK;
	bk_isp_camera_channel_config_t instance;

	if (camera == NULL) {
		return AVDK_ERR_INVAL;
	}

	if (open_ret != NULL) {
		*open_ret = AVDK_ERR_OK;
	}

	if (open_sp && !bk_snapshot_sw_mp_channel_ready(camera)) {
		bk_snapshot_sw_sp_fill_instance(&instance, width, height);
		ret = bk_isp_camera_channel_open(camera, ISP_SP_CHN_ID, &instance);
		if (open_ret != NULL) {
			*open_ret = ret;
		}
		return ret;
	}

	if (!s_mp_frame_sync.sem_inited) {
		if (rtos_init_semaphore(&s_mp_frame_sync.sem, 1) != BK_OK) {
			return AVDK_ERR_GENERIC;
		}
		s_mp_frame_sync.sem_inited = true;
	}

	while (rtos_get_semaphore(&s_mp_frame_sync.sem, 0) == BK_OK) {
	}

	s_mp_frame_sync.waiting = true;
	s_mp_frame_sync.mid_frame_hit = false;
	s_mp_frame_sync.trigger_line = bk_snapshot_sw_mp_mid_trigger_line(camera);
	s_mp_frame_sync.camera = camera;
	s_mp_frame_sync.sp_width = width;
	s_mp_frame_sync.sp_height = height;

	ret = bk_isp_camera_register_isr_callback(camera, ISR_TYPE_16LINE_DONE,
						  bk_snapshot_sw_mp_mid_frame_cb, &s_mp_frame_sync);
	if (ret != AVDK_ERR_OK) {
		s_mp_frame_sync.waiting = false;
		LOGW("register MP mid-frame cb failed ret=%d\r\n", ret);
		return ret;
	}

	if (rtos_get_semaphore(&s_mp_frame_sync.sem, BK_SNAPSHOT_SW_MP_FRAME_WAIT_MS) != BK_OK) {
		LOGW("wait MP mid-frame interrupt timeout %ums\r\n", BK_SNAPSHOT_SW_MP_FRAME_WAIT_MS);
		ret = AVDK_ERR_TIMEOUT;
	}

	(void)bk_isp_camera_deregister_isr_callback(camera, ISR_TYPE_16LINE_DONE, &s_mp_frame_sync);
	s_mp_frame_sync.waiting = false;

	if (open_sp && ret == AVDK_ERR_OK) {
		if (!s_mp_frame_sync.mid_frame_hit) {
			ret = AVDK_ERR_TIMEOUT;
		} else {
			bk_snapshot_sw_sp_fill_instance(&instance, width, height);
			ret = bk_isp_camera_channel_open(camera, ISP_SP_CHN_ID, &instance);
			if (open_ret != NULL) {
				*open_ret = ret;
			}
		}
	}

	return ret;
}

static avdk_err_t bk_snapshot_sw_wait_mp_mid_frame(bk_isp_camera_ctlr_handle_t camera)
{
	return bk_snapshot_sw_sync_at_mp_mid_frame(camera, false, 0, 0, NULL);
}

static void bk_snapshot_sw_sp_state_clear(void)
{
	s_sw_snapshot.sp_channel_open = false;
	s_sw_snapshot.sp_camera = NULL;
	s_sw_snapshot.sp_width = 0;
	s_sw_snapshot.sp_height = 0;
	s_sw_snapshot.sp_needs_warmup = false;
}

avdk_err_t bk_snapshot_sw_sp_channel_open_at_mp_mid_frame(bk_isp_camera_ctlr_handle_t camera,
							  uint16_t width,
							  uint16_t height)
{
	avdk_err_t open_ret = AVDK_ERR_OK;
	avdk_err_t ret;

	ret = bk_snapshot_sw_sync_at_mp_mid_frame(camera, true, width, height, &open_ret);
	if (ret != AVDK_ERR_OK) {
		return ret;
	}

	return open_ret;
}

static avdk_err_t bk_snapshot_sw_sp_get_control(bk_isp_camera_ctlr_handle_t camera,
						isp_control_t **isp_control,
						isp_channel_config_t **sp_cfg)
{
	bk_camera_isp_ctlr_t *control;

	if (camera == NULL || isp_control == NULL || sp_cfg == NULL) {
		return AVDK_ERR_INVAL;
	}

	control = __containerof(camera, bk_camera_isp_ctlr_t, ops);
	*isp_control = (isp_control_t *)control->isp_handle;
	if (*isp_control == NULL || (*isp_control)->pop_buf == NULL || (*isp_control)->free_buf == NULL) {
		return AVDK_ERR_NODEV;
	}

	*sp_cfg = &(*isp_control)->chn[ISP_SP_CHN_ID];
	if (!(*sp_cfg)->enable) {
		LOGE("SP channel not enabled\r\n");
		return AVDK_ERR_NODEV;
	}

	return AVDK_ERR_OK;
}

static avdk_err_t bk_snapshot_sw_sp_drop_frame(bk_isp_camera_ctlr_handle_t camera, uint32_t timeout_ms)
{
	isp_control_t *isp_control = NULL;
	isp_channel_config_t *sp_cfg = NULL;
	VIDEO_BUF_S buf = {0};
	int ret;
	avdk_err_t err;

	err = bk_snapshot_sw_sp_get_control(camera, &isp_control, &sp_cfg);
	if (err != AVDK_ERR_OK) {
		return err;
	}

	ret = isp_control->pop_buf(sp_cfg->channel, &buf, timeout_ms);
	if (ret != BK_OK) {
		LOGE("SP warmup pop_buf failed ret=%d\r\n", ret);
		return AVDK_ERR_TIMEOUT;
	}

	isp_control->free_buf(sp_cfg->channel, &buf);
	return AVDK_ERR_OK;
}

static avdk_err_t bk_snapshot_sw_read_sp_nv12(bk_isp_camera_ctlr_handle_t camera,
						uint8_t *frame,
						uint32_t size,
						uint32_t timeout_ms)
{
	isp_control_t *isp_control = NULL;
	isp_channel_config_t *sp_cfg = NULL;
	VIDEO_BUF_S buf = {0};
	int ret;
	uint32_t copied = 0;
	avdk_err_t err;

	if (frame == NULL || size == 0) {
		return AVDK_ERR_INVAL;
	}

	err = bk_snapshot_sw_sp_get_control(camera, &isp_control, &sp_cfg);
	if (err != AVDK_ERR_OK) {
		return err;
	}

	ret = isp_control->pop_buf(sp_cfg->channel, &buf, timeout_ms);
	if (ret != BK_OK) {
		LOGE("SP pop_buf failed ret=%d\r\n", ret);
		return AVDK_ERR_TIMEOUT;
	}

	if (buf.numPlanes >= 2) {
		uint32_t y_size = buf.planes[0].size;
		uint32_t uv_size = buf.planes[1].size;

		if ((y_size + uv_size) > size) {
			LOGE("SP frame too large y=%u uv=%u need=%u\r\n", y_size, uv_size, size);
			isp_control->free_buf(sp_cfg->channel, &buf);
			return AVDK_ERR_INVAL;
		}

		os_memcpy(frame, (void *)(uintptr_t)buf.planes[0].dmaPhyAddr, y_size);
		os_memcpy(frame + y_size, (void *)(uintptr_t)buf.planes[1].dmaPhyAddr, uv_size);
		copied = y_size + uv_size;
	} else if (buf.numPlanes >= 1) {
		uint32_t copy_len = (buf.imageSize > 0) ? buf.imageSize : buf.planes[0].size;

		if (copy_len > size) {
			copy_len = size;
		}
		os_memcpy(frame, (void *)(uintptr_t)buf.planes[0].dmaPhyAddr, copy_len);
		copied = copy_len;
	}

	isp_control->free_buf(sp_cfg->channel, &buf);

	if (copied == 0) {
		return AVDK_ERR_GENERIC;
	}

	return AVDK_ERR_OK;
}

static avdk_err_t bk_snapshot_sw_sp_channel_ensure(bk_isp_camera_ctlr_handle_t camera,
						   uint16_t width,
						   uint16_t height)
{
	avdk_err_t ret;
	bool channel_on;

	if (camera == NULL) {
		return AVDK_ERR_INVAL;
	}

	channel_on = (bk_isp_camera_channel_state_get(camera, ISP_SP_CHN_ID) ==
		      ISP_CHANNEL_STATE_TURN_ON);
	if (!channel_on && s_sw_snapshot.sp_channel_open) {
		LOGW("SP software state stale after camera off, will reopen\r\n");
		bk_snapshot_sw_sp_state_clear();
	}
	if (channel_on &&
	    s_sw_snapshot.sp_width != 0 &&
	    (s_sw_snapshot.sp_width != width || s_sw_snapshot.sp_height != height)) {
		LOGE("SP channel %ux%u busy, request %ux%u not supported without camera restart\r\n",
		     s_sw_snapshot.sp_width, s_sw_snapshot.sp_height, width, height);
		return AVDK_ERR_BUSY;
	}
	if (channel_on) {
		s_sw_snapshot.sp_channel_open = true;
		s_sw_snapshot.sp_camera = camera;
		s_sw_snapshot.sp_width = width;
		s_sw_snapshot.sp_height = height;
		s_sw_snapshot.sp_needs_warmup = false;
		return AVDK_ERR_OK;
	}

	ret = bk_snapshot_sw_sp_channel_open_at_mp_mid_frame(camera, width, height);
	if (ret != AVDK_ERR_OK) {
		LOGE("SP channel open at MP mid-frame interrupt failed %dx%d ret=%d\r\n", width, height, ret);
		return ret;
	}

	s_sw_snapshot.sp_channel_open = true;
	s_sw_snapshot.sp_camera = camera;
	s_sw_snapshot.sp_width = width;
	s_sw_snapshot.sp_height = height;
	s_sw_snapshot.sp_needs_warmup = true;
	LOGI("SP channel opened %dx%u NV12 frame mode, warmup drop=%u\r\n",
	     width, height, BK_SNAPSHOT_SW_SP_WARMUP_DROP_CNT);
	return AVDK_ERR_OK;
}

static void *bk_snapshot_sw_outbuf_malloc(uint32_t outbuf_size, void *args)
{
	uint32_t size;

	(void)args;

	size = (outbuf_size > 0) ? outbuf_size : BK_SNAPSHOT_SW_JPEG_OUTBUF_SIZE;
	return bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, size);
}

static uint32_t bk_snapshot_sw_outbuf_complete(bk_jpeg_encode_outbuf_info_t *info)
{
	bk_snapshot_sw_enc_ctx_t *ctx = (bk_snapshot_sw_enc_ctx_t *)info->args;

	if (ctx == NULL) {
		return BK_FAIL;
	}

	ctx->enc_status = info->status;
	if (info->status == BK_OK && info->length > 0) {
		ctx->jpeg_data = info->outbuf;
		ctx->jpeg_size = info->length;
	} else {
		ctx->jpeg_data = NULL;
		ctx->jpeg_size = 0;
		bk_snapshot_sw_jpeg_buf_free(info->outbuf);
	}

	return BK_OK;
}

static void bk_snapshot_sw_jpege_destroy(void)
{
	if (s_sw_snapshot.jpege_handle == NULL) {
		return;
	}

	if (s_sw_snapshot.jpege_open) {
		(void)bk_jpeg_encode_close(s_sw_snapshot.jpege_handle);
		s_sw_snapshot.jpege_open = false;
	}

	(void)bk_jpeg_encode_deinit(s_sw_snapshot.jpege_handle);
	(void)bk_jpeg_encode_delete(s_sw_snapshot.jpege_handle);
	s_sw_snapshot.jpege_handle = NULL;
	s_sw_snapshot.jpege_width = 0;
	s_sw_snapshot.jpege_height = 0;
}

static avdk_err_t bk_snapshot_sw_jpege_ensure(uint16_t width, uint16_t height, uint8_t quality)
{
	avdk_err_t ret;
	bk_jpeg_encode_frame_config_t cfg = {0};

	if (s_sw_snapshot.jpege_handle != NULL && s_sw_snapshot.jpege_open &&
	    s_sw_snapshot.jpege_width == width && s_sw_snapshot.jpege_height == height) {
		(void)bk_jpeg_encode_ioctl(s_sw_snapshot.jpege_handle, BK_JPEG_ENCODE_IOCTL_SET_QUALITY, &quality);
		return AVDK_ERR_OK;
	}

	bk_snapshot_sw_jpege_destroy();

	cfg.width = width;
	cfg.height = height;
	cfg.input_format = BK_PIXEL_FORMAT_NV12;
	cfg.input_buf = (uint32_t)(uintptr_t)s_sw_snapshot.nv12_buf;
	cfg.input_size = (uint32_t)width * height * 3U / 2U;
	cfg.quality = quality;
	cfg.outbuf_malloc = bk_snapshot_sw_outbuf_malloc;
	cfg.outbuf_complete = bk_snapshot_sw_outbuf_complete;
	cfg.outbuf_complete_args = &s_sw_enc_ctx;

	ret = bk_jpeg_encode_frame_new(&s_sw_snapshot.jpege_handle, &cfg);
	if (ret != AVDK_ERR_OK) {
		LOGE("jpeg frame new failed ret=%d\r\n", ret);
		return ret;
	}

	ret = bk_jpeg_encode_init(s_sw_snapshot.jpege_handle);
	if (ret != AVDK_ERR_OK) {
		bk_snapshot_sw_jpege_destroy();
		return ret;
	}

	ret = bk_jpeg_encode_open(s_sw_snapshot.jpege_handle);
	if (ret != AVDK_ERR_OK) {
		(void)bk_jpeg_encode_deinit(s_sw_snapshot.jpege_handle);
		bk_snapshot_sw_jpege_destroy();
		return ret;
	}

	s_sw_snapshot.jpege_open = true;
	s_sw_snapshot.jpege_width = width;
	s_sw_snapshot.jpege_height = height;
	return AVDK_ERR_OK;
}

static avdk_err_t bk_snapshot_sw_encode_nv12(uint16_t width,
					     uint16_t height,
					     uint8_t *nv12,
					     uint8_t quality,
					     uint8_t **out_jpeg,
					     uint32_t *out_size)
{
	bk_jpeg_encode_input_t enc_in = {0};
	avdk_err_t ret;

	if (nv12 == NULL || out_jpeg == NULL || out_size == NULL) {
		return AVDK_ERR_INVAL;
	}

	s_sw_enc_ctx.jpeg_data = NULL;
	s_sw_enc_ctx.jpeg_size = 0;
	s_sw_enc_ctx.enc_status = (uint32_t)~0U;

	ret = bk_snapshot_sw_jpege_ensure(width, height, quality);
	if (ret != AVDK_ERR_OK) {
		return ret;
	}

	enc_in.pic_buf = (uint32_t)(uintptr_t)nv12;

	ret = bk_jpeg_encode_frame(s_sw_snapshot.jpege_handle, &enc_in);
	if (ret != AVDK_ERR_OK) {
		LOGE("jpeg encode_frame failed ret=%d\r\n", ret);
		return ret;
	}

	if (s_sw_enc_ctx.enc_status != BK_OK || s_sw_enc_ctx.jpeg_size == 0 ||
	    s_sw_enc_ctx.jpeg_data == NULL) {
		LOGE("jpeg encode failed status=%u size=%u\r\n",
		     s_sw_enc_ctx.enc_status, s_sw_enc_ctx.jpeg_size);
		return AVDK_ERR_GENERIC;
	}

	*out_jpeg = s_sw_enc_ctx.jpeg_data;
	*out_size = s_sw_enc_ctx.jpeg_size;
	s_sw_enc_ctx.jpeg_data = NULL;
	return AVDK_ERR_OK;
}

avdk_err_t bk_snapshot_sw_capture(bk_snapshot_sw_config_t *config,
				  bk_snapshot_image_t *out_image)
{
	avdk_err_t ret = AVDK_ERR_OK;
	uint16_t width;
	uint16_t height;
	uint32_t timeout_ms;
	uint32_t frame_timeout_ms;
	uint8_t quality;
	uint32_t nv12_size;
	uint8_t *jpeg_buf = NULL;
	uint32_t jpeg_size = 0;
	uint32_t i;
	bk_isp_camera_ctlr_handle_t camera;

	if (config == NULL || out_image == NULL || config->camera_handle == NULL) {
		return AVDK_ERR_INVAL;
	}

	os_memset(out_image, 0, sizeof(*out_image));
	camera = config->camera_handle;

	width = (config->width > 0) ? config->width : BK_SNAPSHOT_SW_DEFAULT_WIDTH;
	height = (config->height > 0) ? config->height : BK_SNAPSHOT_SW_DEFAULT_HEIGHT;
	if (width != BK_SNAPSHOT_SW_DEFAULT_WIDTH || height != BK_SNAPSHOT_SW_DEFAULT_HEIGHT) {
		LOGE("sw snapshot only supports %ux%u now\r\n",
		     BK_SNAPSHOT_SW_DEFAULT_WIDTH, BK_SNAPSHOT_SW_DEFAULT_HEIGHT);
		return AVDK_ERR_UNSUPPORTED;
	}

	timeout_ms = (config->read_timeout_ms > 0) ? config->read_timeout_ms :
						     BK_SNAPSHOT_SW_DEFAULT_TIMEOUT_MS;
	quality = bk_snapshot_sw_map_jpege_quality(config->jpeg_quality);

	ret = bk_snapshot_sw_mutex_lock();
	if (ret != AVDK_ERR_OK) {
		return ret;
	}

	ret = bk_snapshot_sw_buffers_ensure();
	if (ret != AVDK_ERR_OK) {
		goto cleanup;
	}

	ret = bk_snapshot_sw_sp_channel_ensure(camera, width, height);
	if (ret != AVDK_ERR_OK) {
		goto cleanup;
	}

	nv12_size = (uint32_t)width * height * 3U / 2U;
	frame_timeout_ms = timeout_ms / (BK_SNAPSHOT_SW_SP_WARMUP_DROP_CNT + 1U);
	if (frame_timeout_ms == 0) {
		frame_timeout_ms = timeout_ms;
	}

	if (s_sw_snapshot.sp_needs_warmup) {
		for (i = 0; i < BK_SNAPSHOT_SW_SP_WARMUP_DROP_CNT; i++) {
			ret = bk_snapshot_sw_sp_drop_frame(camera, frame_timeout_ms);
			if (ret != AVDK_ERR_OK) {
				LOGE("SP warmup drop %u failed ret=%d\r\n", i, ret);
				goto cleanup;
			}
		}
		s_sw_snapshot.sp_needs_warmup = false;
	}

	ret = bk_snapshot_sw_wait_mp_mid_frame(camera);
	if (ret != AVDK_ERR_OK) {
		LOGW("wait MP mid-frame before read failed ret=%d\r\n", ret);
	}

	ret = bk_snapshot_sw_read_sp_nv12(camera, s_sw_snapshot.nv12_buf,
					  nv12_size, frame_timeout_ms);
	if (ret != AVDK_ERR_OK) {
		LOGE("SP frame read failed ret=%d\r\n", ret);
		goto cleanup;
	}

	ret = bk_snapshot_sw_encode_nv12(width, height, s_sw_snapshot.nv12_buf, quality,
					 &jpeg_buf, &jpeg_size);
	if (ret != AVDK_ERR_OK) {
		goto cleanup;
	}

	out_image->data = jpeg_buf;
	out_image->size = jpeg_size;
	out_image->width = width;
	out_image->height = height;
	out_image->free_fn = bk_snapshot_sw_default_free;
	out_image->free_arg = NULL;
	jpeg_buf = NULL;

	LOGI("sw snapshot ok size=%u %ux%u\r\n", out_image->size, width, height);

cleanup:
	bk_snapshot_sw_jpeg_buf_free(jpeg_buf);
	bk_snapshot_sw_mutex_unlock();
	return ret;
}

avdk_err_t bk_snapshot_sw_prepare(void)
{
	return bk_snapshot_sw_buffers_ensure();
}

avdk_err_t bk_snapshot_sw_deinit(void)
{
	if (!s_sw_snapshot.mutex_inited) {
		return AVDK_ERR_OK;
	}

	if (bk_snapshot_sw_mutex_lock() != AVDK_ERR_OK) {
		return AVDK_ERR_GENERIC;
	}

	bk_snapshot_sw_jpege_destroy();
	bk_snapshot_sw_nv12_buf_free();

	if (s_sw_snapshot.sp_channel_open && s_sw_snapshot.sp_camera != NULL) {
		(void)bk_isp_camera_channel_close(s_sw_snapshot.sp_camera, ISP_SP_CHN_ID);
		bk_snapshot_sw_sp_state_clear();
	}

	rtos_unlock_mutex(&s_sw_snapshot.mutex);
	rtos_deinit_mutex(&s_sw_snapshot.mutex);
	if (s_mp_frame_sync.sem_inited) {
		rtos_deinit_semaphore(&s_mp_frame_sync.sem);
	}
	os_memset(&s_sw_snapshot, 0, sizeof(s_sw_snapshot));
	os_memset(&s_sw_enc_ctx, 0, sizeof(s_sw_enc_ctx));
	os_memset(&s_mp_frame_sync, 0, sizeof(s_mp_frame_sync));
	return AVDK_ERR_OK;
}
