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
#include <components/bk_flexa_bond.h>
#include <modules/jpeg_enc_sw.h>
#include <driver/isp_types.h>
#include "bk_snapshot_sw.h"
#include <common/avdk_pixel_types.h>
#include <driver/isp_base.h>
#include <components/bk_frame_buffer.h>

#define TAG "bk_snap_sw"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BK_SNAPSHOT_SW_YUYV_FRAME_BYTES  (BK_SNAPSHOT_SW_DEFAULT_WIDTH * BK_SNAPSHOT_SW_DEFAULT_HEIGHT * 2U)
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
	bool sp_owned_by_snapshot;
	bool sp_needs_warmup;
	uint16_t sp_width;
	uint16_t sp_height;
	bk_isp_camera_ctlr_handle_t sp_camera;
	uint8_t *yuyv_buf;
} bk_snapshot_sw_module_ctx_t;

static bk_snapshot_sw_module_ctx_t s_sw_snapshot = {0};

typedef struct {
	beken_semaphore_t sem;
	bool sem_inited;
	bool waiting;
	bool frame_complete_hit;
	bk_isp_camera_ctlr_handle_t camera;
	uint16_t sp_width;
	uint16_t sp_height;
} bk_snapshot_sw_mp_sync_t;

static bk_snapshot_sw_mp_sync_t s_mp_frame_sync = {0};

static avdk_err_t bk_snapshot_sw_buffers_ensure(void)
{
	if (s_sw_snapshot.yuyv_buf == NULL) {
		s_sw_snapshot.yuyv_buf = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, BK_SNAPSHOT_SW_YUYV_FRAME_BYTES);
		if (s_sw_snapshot.yuyv_buf == NULL) {
			LOGE("alloc yuyv buf failed, need %u bytes\r\n", BK_SNAPSHOT_SW_YUYV_FRAME_BYTES);
			return AVDK_ERR_NOMEM;
		}
	}

	return AVDK_ERR_OK;
}

static void bk_snapshot_sw_work_buf_free(void)
{
	if (s_sw_snapshot.yuyv_buf != NULL) {
		bk_frame_buffer_free(s_sw_snapshot.yuyv_buf);
		s_sw_snapshot.yuyv_buf = NULL;
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
	if (quality > 100) {
		return 100;
	}
	return quality;
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

static void bk_snapshot_sw_mp_frame_complete_cb(uint32_t seq, uint32_t line, uint8_t chnl, uint8_t ok, void *arg)
{
	(void)seq;
	(void)line;
	(void)arg;

	if (chnl != ISP_MP_CHN_ID || !ok) {
		return;
	}

	if (s_mp_frame_sync.waiting && !s_mp_frame_sync.frame_complete_hit) {
		s_mp_frame_sync.frame_complete_hit = true;
		rtos_set_semaphore(&s_mp_frame_sync.sem);
	}
}

static avdk_err_t bk_snapshot_sw_sync_at_mp_frame_complete(bk_isp_camera_ctlr_handle_t camera,
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
	s_mp_frame_sync.frame_complete_hit = false;
	s_mp_frame_sync.camera = camera;
	s_mp_frame_sync.sp_width = width;
	s_mp_frame_sync.sp_height = height;

	ret = bk_isp_camera_register_isr_callback(camera, ISR_TYPE_FRAME_COMPLETE,
						  bk_snapshot_sw_mp_frame_complete_cb, &s_mp_frame_sync);
	if (ret != AVDK_ERR_OK) {
		s_mp_frame_sync.waiting = false;
		LOGW("register MP frame-complete cb failed ret=%d\r\n", ret);
		return ret;
	}

	if (rtos_get_semaphore(&s_mp_frame_sync.sem, BK_SNAPSHOT_SW_MP_FRAME_WAIT_MS) != BK_OK) {
		LOGW("wait MP frame-complete interrupt timeout %ums\r\n", BK_SNAPSHOT_SW_MP_FRAME_WAIT_MS);
		ret = AVDK_ERR_TIMEOUT;
	}

	(void)bk_isp_camera_deregister_isr_callback(camera, ISR_TYPE_FRAME_COMPLETE, &s_mp_frame_sync);
	s_mp_frame_sync.waiting = false;

	if (open_sp && ret == AVDK_ERR_OK) {
		if (!s_mp_frame_sync.frame_complete_hit) {
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

static avdk_err_t bk_snapshot_sw_wait_mp_frame_complete(bk_isp_camera_ctlr_handle_t camera)
{
	return bk_snapshot_sw_sync_at_mp_frame_complete(camera, false, 0, 0, NULL);
}

static void bk_snapshot_sw_sp_state_clear(void)
{
	s_sw_snapshot.sp_channel_open = false;
	s_sw_snapshot.sp_owned_by_snapshot = false;
	s_sw_snapshot.sp_camera = NULL;
	s_sw_snapshot.sp_width = 0;
	s_sw_snapshot.sp_height = 0;
	s_sw_snapshot.sp_needs_warmup = false;
}

avdk_err_t bk_snapshot_sw_sp_channel_open_at_mp_frame_complete(bk_isp_camera_ctlr_handle_t camera,
							      uint16_t width,
							      uint16_t height)
{
	avdk_err_t open_ret = AVDK_ERR_OK;
	avdk_err_t ret;

	ret = bk_snapshot_sw_sync_at_mp_frame_complete(camera, true, width, height, &open_ret);
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
		s_sw_snapshot.sp_owned_by_snapshot = false;
		s_sw_snapshot.sp_camera = camera;
		s_sw_snapshot.sp_width = width;
		s_sw_snapshot.sp_height = height;
		s_sw_snapshot.sp_needs_warmup = false;
		return AVDK_ERR_OK;
	}

	ret = bk_snapshot_sw_sync_at_mp_frame_complete(camera, true, width, height, NULL);
	if (ret != AVDK_ERR_OK) {
		LOGE("SP channel open at MP frame-complete interrupt failed %dx%d ret=%d\r\n", width, height, ret);
		return ret;
	}

	s_sw_snapshot.sp_channel_open = true;
	s_sw_snapshot.sp_owned_by_snapshot = true;
	s_sw_snapshot.sp_camera = camera;
	s_sw_snapshot.sp_width = width;
	s_sw_snapshot.sp_height = height;
	s_sw_snapshot.sp_needs_warmup = true;
	LOGI("SP channel opened %dx%u NV12 frame mode, warmup drop=%u\r\n",
	     width, height, BK_SNAPSHOT_SW_SP_WARMUP_DROP_CNT);
	return AVDK_ERR_OK;
}

static avdk_err_t bk_snapshot_sw_nv12_to_yuyv(uint16_t width,
					      uint16_t height,
					      const uint8_t *y_plane,
					      const uint8_t *uv_plane,
					      uint8_t *yuyv)
{
	uint32_t y;
	uint32_t y_stride = width;
	uint32_t yuyv_stride = width * 2U;

	if (y_plane == NULL || uv_plane == NULL || yuyv == NULL ||
	    (width & 1U) || (height & 1U)) {
		return AVDK_ERR_INVAL;
	}

	/* NV12 UV is 2x2 subsampled: process two Y rows per UV row. */
	for (y = 0; y < height; y += 2U) {
		const uint8_t *y_row0 = y_plane + y * y_stride;
		const uint8_t *y_row1 = y_row0 + y_stride;
		const uint8_t *uv_row = uv_plane + (y >> 1) * y_stride;
		uint8_t *out_row0 = yuyv + y * yuyv_stride;
		uint8_t *out_row1 = out_row0 + yuyv_stride;
		uint32_t x = 0;

		for (; x + 8U <= width; x += 8U) {
			const uint8_t *uv = uv_row + x;
			uint8_t *d0 = out_row0 + (x * 2U);
			uint8_t *d1 = out_row1 + (x * 2U);
			uint32_t pack0;
			uint32_t pack1;

			pack0 = ((uint32_t)uv[1] << 24) | ((uint32_t)y_row0[x + 1U] << 16) |
				((uint32_t)uv[0] << 8) | y_row0[x];
			pack1 = ((uint32_t)uv[1] << 24) | ((uint32_t)y_row1[x + 1U] << 16) |
				((uint32_t)uv[0] << 8) | y_row1[x];
			*(uint32_t *)d0 = pack0;
			*(uint32_t *)d1 = pack1;

			pack0 = ((uint32_t)uv[3] << 24) | ((uint32_t)y_row0[x + 3U] << 16) |
				((uint32_t)uv[2] << 8) | y_row0[x + 2U];
			pack1 = ((uint32_t)uv[3] << 24) | ((uint32_t)y_row1[x + 3U] << 16) |
				((uint32_t)uv[2] << 8) | y_row1[x + 2U];
			*(uint32_t *)(d0 + 4U) = pack0;
			*(uint32_t *)(d1 + 4U) = pack1;

			pack0 = ((uint32_t)uv[5] << 24) | ((uint32_t)y_row0[x + 5U] << 16) |
				((uint32_t)uv[4] << 8) | y_row0[x + 4U];
			pack1 = ((uint32_t)uv[5] << 24) | ((uint32_t)y_row1[x + 5U] << 16) |
				((uint32_t)uv[4] << 8) | y_row1[x + 4U];
			*(uint32_t *)(d0 + 8U) = pack0;
			*(uint32_t *)(d1 + 8U) = pack1;

			pack0 = ((uint32_t)uv[7] << 24) | ((uint32_t)y_row0[x + 7U] << 16) |
				((uint32_t)uv[6] << 8) | y_row0[x + 6U];
			pack1 = ((uint32_t)uv[7] << 24) | ((uint32_t)y_row1[x + 7U] << 16) |
				((uint32_t)uv[6] << 8) | y_row1[x + 6U];
			*(uint32_t *)(d0 + 12U) = pack0;
			*(uint32_t *)(d1 + 12U) = pack1;
		}

		for (; x < width; x += 2U) {
			uint8_t u = uv_row[x];
			uint8_t v = uv_row[x + 1U];
			uint32_t pack0 = ((uint32_t)v << 24) | ((uint32_t)y_row0[x + 1U] << 16) |
					 ((uint32_t)u << 8) | y_row0[x];
			uint32_t pack1 = ((uint32_t)v << 24) | ((uint32_t)y_row1[x + 1U] << 16) |
					 ((uint32_t)u << 8) | y_row1[x];

			*(uint32_t *)(out_row0 + (x * 2U)) = pack0;
			*(uint32_t *)(out_row1 + (x * 2U)) = pack1;
		}
	}

	return AVDK_ERR_OK;
}

static avdk_err_t bk_snapshot_sw_encode_yuyv(uint16_t width,
					     uint16_t height,
					     uint8_t *yuyv,
					     uint8_t quality,
					     uint8_t **out_jpeg,
					     uint32_t *out_size)
{
	uint8_t *jpeg_buf;
	uint16_t header_len = 0;
	int enc_size = 0;
	int ret;

	if (yuyv == NULL || out_jpeg == NULL || out_size == NULL) {
		return AVDK_ERR_INVAL;
	}

	jpeg_buf = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED,
						     BK_SNAPSHOT_SW_JPEG_OUTBUF_SIZE);
	if (jpeg_buf == NULL) {
		LOGE("alloc sw jpeg outbuf failed, need %u bytes\r\n",
		     BK_SNAPSHOT_SW_JPEG_OUTBUF_SIZE);
		return AVDK_ERR_NOMEM;
	}

	jpeg_sw_encoder_init();
	ret = jpeg_sw_encoder.open(&jpeg_sw_encoder.codec, width, height, yuyv,
				   jpeg_buf, &header_len, quality);
	if (ret != BK_OK) {
		LOGE("jpeg sw open failed ret=%d\r\n", ret);
		bk_snapshot_sw_jpeg_buf_free(jpeg_buf);
		return AVDK_ERR_GENERIC;
	}

	ret = jpeg_sw_encoder.enc(jpeg_sw_encoder.codec, jpeg_buf + header_len,
				  BK_SNAPSHOT_SW_JPEG_OUTBUF_SIZE - header_len, &enc_size);
	(void)jpeg_sw_encoder.deinit(&jpeg_sw_encoder.codec);
	if (ret != BK_OK || enc_size <= 0) {
		LOGE("jpeg sw encode failed ret=%d size=%d\r\n", ret, enc_size);
		bk_snapshot_sw_jpeg_buf_free(jpeg_buf);
		return AVDK_ERR_GENERIC;
	}

	*out_jpeg = jpeg_buf;
	*out_size = (uint32_t)header_len + (uint32_t)enc_size;
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
	uint32_t y_plane_size;
	uint8_t *jpeg_buf = NULL;
	uint32_t jpeg_size = 0;
	uint32_t i;
	bk_isp_camera_ctlr_handle_t camera;
	isp_control_t *isp_control = NULL;
	isp_channel_config_t *sp_cfg = NULL;
	VIDEO_BUF_S sp_buf = {0};
	bool sp_buf_held = false;
	const uint8_t *nv12_y = NULL;
	const uint8_t *nv12_uv = NULL;

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
	y_plane_size = (uint32_t)width * height;
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

	ret = bk_snapshot_sw_wait_mp_frame_complete(camera);
	if (ret != AVDK_ERR_OK) {
		LOGW("wait MP frame-complete before read failed ret=%d\r\n", ret);
	}

	ret = bk_snapshot_sw_sp_get_control(camera, &isp_control, &sp_cfg);
	if (ret == AVDK_ERR_OK) {
		int pop_ret = isp_control->pop_buf(sp_cfg->channel, &sp_buf, frame_timeout_ms);
		if (pop_ret == BK_OK) {
			sp_buf_held = true;
		} else {
			LOGE("SP pop_buf failed ret=%d\r\n", pop_ret);
			ret = AVDK_ERR_TIMEOUT;
		}
	}
	if (ret == AVDK_ERR_OK) {
		if (sp_buf.numPlanes >= 2) {
			uintptr_t y_addr = (uintptr_t)sp_buf.planes[0].dmaPhyAddr;
			uintptr_t uv_addr = (uintptr_t)sp_buf.planes[1].dmaPhyAddr;
			uint32_t y_size = sp_buf.planes[0].size;
			uint32_t uv_size = sp_buf.planes[1].size;

			if (y_addr != 0 && uv_addr != 0 && y_size >= y_plane_size &&
			    uv_size >= (nv12_size - y_plane_size)) {
				nv12_y = (const uint8_t *)y_addr;
				nv12_uv = (const uint8_t *)uv_addr;
			} else {
				ret = AVDK_ERR_INVAL;
			}
		} else if (sp_buf.numPlanes >= 1) {
			uintptr_t y_addr = (uintptr_t)sp_buf.planes[0].dmaPhyAddr;
			uint32_t frame_size = sp_buf.imageSize ? sp_buf.imageSize : sp_buf.planes[0].size;

			if (y_addr != 0 && frame_size >= nv12_size) {
				nv12_y = (const uint8_t *)y_addr;
				nv12_uv = nv12_y + y_plane_size;
			} else {
				ret = AVDK_ERR_INVAL;
			}
		} else {
			ret = AVDK_ERR_INVAL;
		}
	}
	if (ret != AVDK_ERR_OK) {
		LOGE("SP frame read failed ret=%d\r\n", ret);
		goto cleanup;
	}

	ret = bk_snapshot_sw_nv12_to_yuyv(width, height, nv12_y, nv12_uv,
					  s_sw_snapshot.yuyv_buf);
	if (ret != AVDK_ERR_OK) {
		LOGE("NV12 to YUYV failed ret=%d\r\n", ret);
		goto cleanup;
	}

	ret = bk_snapshot_sw_encode_yuyv(width, height, s_sw_snapshot.yuyv_buf, quality,
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
	if (sp_buf_held && isp_control != NULL && sp_cfg != NULL) {
		isp_control->free_buf(sp_cfg->channel, &sp_buf);
		sp_buf_held = false;
	}
	if (s_sw_snapshot.sp_owned_by_snapshot && s_sw_snapshot.sp_camera == camera) {
		(void)bk_isp_camera_channel_close(camera, ISP_SP_CHN_ID);
		bk_snapshot_sw_sp_state_clear();
		LOGI("SP channel closed after snapshot\r\n");
	}
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

	bk_snapshot_sw_work_buf_free();

	if (s_sw_snapshot.sp_channel_open && s_sw_snapshot.sp_owned_by_snapshot &&
	    s_sw_snapshot.sp_camera != NULL) {
		(void)bk_isp_camera_channel_close(s_sw_snapshot.sp_camera, ISP_SP_CHN_ID);
		bk_snapshot_sw_sp_state_clear();
	}

	rtos_unlock_mutex(&s_sw_snapshot.mutex);
	rtos_deinit_mutex(&s_sw_snapshot.mutex);
	if (s_mp_frame_sync.sem_inited) {
		rtos_deinit_semaphore(&s_mp_frame_sync.sem);
	}
	os_memset(&s_sw_snapshot, 0, sizeof(s_sw_snapshot));
	os_memset(&s_mp_frame_sync, 0, sizeof(s_mp_frame_sync));
	return AVDK_ERR_OK;
}
