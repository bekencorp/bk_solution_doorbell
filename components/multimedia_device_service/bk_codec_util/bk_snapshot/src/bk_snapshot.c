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
#include <sdkconfig.h>
#include <components/log.h>
#include <components/bk_flexa_bond.h>
#include <components/bk_encode/bk_jpeg_encode_ctlr.h>
#include <components/bk_encode/bk_h264_encode_ctlr.h>
#include "bk_snapshot.h"
#include <driver/isp_base.h>
#include <driver/isp_types.h>
#include "private_snapshot.h"
#include <components/bk_frame_buffer.h>
#define TAG "bk_snap"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

typedef struct {
	beken_semaphore_t done_sem;
	bk_snapshot_image_t image;
	uint32_t capture_ok;
} bk_snapshot_capture_ctx_t;

typedef struct {
	beken_mutex_t mutex;
	bool mutex_inited;
	bk_jpeg_encode_ctlr_handle_t jpege_handle;
	bool jpege_open;
	uint32_t jpege_width;
	uint32_t jpege_height;
	bk_snapshot_capture_ctx_t *active_capture;
	void *jpege_outbuf_pool;
	void *retained_outbuf;
} bk_snapshot_module_ctx_t;

static bk_snapshot_module_ctx_t s_snapshot = {0};

#define BK_SNAPSHOT_HW_DRAIN_MS        50U
#define BK_SNAPSHOT_H264_STOP_DRAIN_MS 100U
#define BK_SNAPSHOT_JPEG_STOP_DRAIN_MS 50U
/* 1080p JPEG Q5 is typically < 100KB; 256KB is enough for snapshot output. */
#define BK_SNAPSHOT_JPEG_OUTBUF_SIZE (256U * 1024U)

static void bk_snapshot_hw_drain(void)
{
	rtos_delay_milliseconds(BK_SNAPSHOT_HW_DRAIN_MS);
}

static void bk_snapshot_drain(uint32_t ms)
{
	rtos_delay_milliseconds(ms);
}

static void bk_snapshot_outbuf_pool_free(void)
{
	if (s_snapshot.jpege_outbuf_pool == NULL ||
	    s_snapshot.jpege_outbuf_pool == s_snapshot.retained_outbuf) {
		s_snapshot.jpege_outbuf_pool = NULL;
		return;
	}

	LOGW("free jpeg outbuf pool %p\r\n", s_snapshot.jpege_outbuf_pool);
	bk_frame_buffer_free(s_snapshot.jpege_outbuf_pool);
	s_snapshot.jpege_outbuf_pool = NULL;
}


static avdk_err_t bk_snapshot_outbuf_pool_alloc(void)
{
	bk_snapshot_outbuf_pool_free();
	s_snapshot.jpege_outbuf_pool = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, BK_SNAPSHOT_JPEG_OUTBUF_SIZE);
	if (s_snapshot.jpege_outbuf_pool == NULL) {
		LOGE("jpeg outbuf pool alloc failed, size=%u\r\n", BK_SNAPSHOT_JPEG_OUTBUF_SIZE);
		return AVDK_ERR_NOMEM;
	}

	return AVDK_ERR_OK;
}

static void bk_snapshot_jpege_destroy(bool encode_done)
{
	bk_snapshot_outbuf_pool_free();

	if (s_snapshot.jpege_handle == NULL) {
		return;
	}

	if (s_snapshot.jpege_open) {
		if (!encode_done) {
			LOGW("jpege destroy without encode done, stop encode first\r\n");
			(void)bk_jpeg_encode_ioctl(s_snapshot.jpege_handle, BK_JPEG_ENCODE_IOCTL_STOP_ENCODE, NULL);
		}
		bk_snapshot_drain(BK_SNAPSHOT_JPEG_STOP_DRAIN_MS);
		(void)bk_jpeg_encode_close(s_snapshot.jpege_handle);
		s_snapshot.jpege_open = false;
	}

	(void)bk_jpeg_encode_deinit(s_snapshot.jpege_handle);
	(void)bk_jpeg_encode_delete(s_snapshot.jpege_handle);
	s_snapshot.jpege_handle = NULL;
	s_snapshot.jpege_width = 0;
	s_snapshot.jpege_height = 0;
}

static void bk_snapshot_default_free(void *data, void *user_data)
{
	(void)user_data;
	if (data != NULL) {
		if (data == s_snapshot.retained_outbuf) {
			s_snapshot.retained_outbuf = NULL;
		}
		bk_frame_buffer_free(data);
	}
}

static void *bk_snapshot_outbuf_malloc(uint32_t outbuf_size, void *args)
{
	void *buf;

	(void)outbuf_size;
	(void)args;

	/* JPEG driver requests CONFIG_BK_ENCODER_MJPEG_MAX_OUTPUT_BUFFER,
	 * but snapshot pool is smaller; always prefer the pre-allocated pool.
	 */
	if (s_snapshot.jpege_outbuf_pool != NULL) {
		buf = s_snapshot.jpege_outbuf_pool;
		s_snapshot.jpege_outbuf_pool = NULL;
		return buf;
	}

	buf = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, BK_SNAPSHOT_JPEG_OUTBUF_SIZE);
	if (buf == NULL) {
		LOGE("jpeg outbuf bk_frame_buffer_malloc failed, size=%u\r\n", BK_SNAPSHOT_JPEG_OUTBUF_SIZE);
	}
	return buf;
}

static uint32_t bk_snapshot_outbuf_complete(bk_jpeg_encode_outbuf_info_t *info)
{
	bk_snapshot_capture_ctx_t *ctx = s_snapshot.active_capture;

	if (info == NULL || info->outbuf == NULL) {
		LOGE("outbuf complete invalid, info=%p outbuf=%p\r\n",
		     info, info ? info->outbuf : NULL);
		return BK_FAIL;
	}

	if (ctx == NULL) {
		if (info->outbuf == s_snapshot.retained_outbuf) {
			return BK_OK;
		}
		LOGE("late outbuf callback after capture cleared, outbuf=%p status=%d len=%u\r\n",
		     info->outbuf, info->status, info->length);
		if (info->status != BK_OK && info->outbuf != NULL) {
			if (s_snapshot.jpege_outbuf_pool == NULL) {
				s_snapshot.jpege_outbuf_pool = info->outbuf;
			} else if (info->outbuf != s_snapshot.jpege_outbuf_pool) {
				bk_frame_buffer_free(info->outbuf);
			}
		}

		return BK_OK;
	}

	if (info->status == BK_OK && info->length > 0) {
		ctx->image.data = info->outbuf;
		ctx->image.size = info->length;
		ctx->image.free_fn = bk_snapshot_default_free;
		ctx->image.free_arg = NULL;
		ctx->capture_ok = 1;
		rtos_set_semaphore(&ctx->done_sem);
	} else if (info->outbuf != NULL) {
		LOGE("encode frame failed, status=%d len=%u outbuf=%p\r\n",
		     info->status, info->length, info->outbuf);
		if (ctx->capture_ok != 0) {
			LOGW("late encode failure after capture ok, outbuf=%p image=%p\r\n",
			     info->outbuf, ctx->image.data);
			if (info->outbuf == ctx->image.data) {
				return BK_OK;
			}
		}
		/* Recycle pre-allocated pool buffer on failed frames. */
		if (s_snapshot.jpege_outbuf_pool == NULL) {
			LOGW("recycle outbuf to pool %p\r\n", info->outbuf);
			s_snapshot.jpege_outbuf_pool = info->outbuf;
		} else {
			LOGW("free duplicate outbuf %p\r\n", info->outbuf);
			bk_frame_buffer_free(info->outbuf);
		}
		if (ctx->capture_ok == 0) {
			rtos_set_semaphore(&ctx->done_sem);
		}
	}

	return BK_OK;
}

static avdk_err_t bk_snapshot_mutex_lock(void)
{
	if (!s_snapshot.mutex_inited) {
		if (rtos_init_mutex(&s_snapshot.mutex) != BK_OK) {
			return AVDK_ERR_GENERIC;
		}
		s_snapshot.mutex_inited = true;
	}

	rtos_lock_mutex(&s_snapshot.mutex);
	return AVDK_ERR_OK;
}

static void bk_snapshot_mutex_unlock(void)
{
	if (s_snapshot.mutex_inited) {
		rtos_unlock_mutex(&s_snapshot.mutex);
	}
}

static avdk_err_t bk_snapshot_restore_h264_bond(bk_snapshot_config_t *config)
{
	avdk_err_t ret = AVDK_ERR_OK;

	if (config == NULL || config->source_handle == NULL || config->h264_handle == NULL ||
	    config->h264_bond == NULL) {
		return AVDK_ERR_INVAL;
	}

	ret = bk_flexa_isp_h264e_bond_start(config->h264_bond, config->source_handle, config->h264_handle);
	if (ret != AVDK_ERR_OK) {
		LOGE("restore h264 bond failed %d\r\n", ret);
		return ret;
	}

	(void)bk_h264_encode_force_idr(config->h264_handle);
	return AVDK_ERR_OK;
}

static avdk_err_t bk_snapshot_ensure_jpege(bk_snapshot_config_t *config,
					       isp_control_t *isp_control)
{
	avdk_err_t ret = AVDK_ERR_OK;
	uint8_t chnl_id = ISP_MP_CHN_ID;
	uint32_t width = isp_control->chn[chnl_id].chn_attr.chnFormat.width;
	uint32_t height = isp_control->chn[chnl_id].chn_attr.chnFormat.height;
	uint8_t quality = config->jpeg_quality ? config->jpeg_quality : BK_SNAPSHOT_DEFAULT_QUALITY;

	bk_snapshot_jpege_destroy(false);

	ret = bk_snapshot_outbuf_pool_alloc();
	if (ret != AVDK_ERR_OK) {
		return ret;
	}

	bk_jpeg_encode_hw_flexa_config_t jpege_cfg = {
		.width = width,
		.height = height,
		.input_format = BK_PIXEL_FORMAT_NV12,
		.input_flexa_cnt = 3,
		.input_buf = isp_control->chn[chnl_id].y_addr,
		.input_size = isp_control->chn[chnl_id].buf_cnt,
		.quality = quality,
		.outbuf_malloc = bk_snapshot_outbuf_malloc,
		.outbuf_malloc_args = NULL,
		.outbuf_complete = bk_snapshot_outbuf_complete,
		.outbuf_complete_args = NULL,
	};

	ret = bk_jpeg_encode_hw_flexa_new(&s_snapshot.jpege_handle, &jpege_cfg);
	if (ret != AVDK_ERR_OK) {
		LOGE("create jpege failed %d\r\n", ret);
		return ret;
	}

	ret = bk_jpeg_encode_init(s_snapshot.jpege_handle);
	if (ret != AVDK_ERR_OK) {
		goto err;
	}

	ret = bk_jpeg_encode_open(s_snapshot.jpege_handle);
	if (ret != AVDK_ERR_OK) {
		(void)bk_jpeg_encode_deinit(s_snapshot.jpege_handle);
		goto err;
	}

	s_snapshot.jpege_open = true;
	s_snapshot.jpege_width = width;
	s_snapshot.jpege_height = height;
	return AVDK_ERR_OK;

err:
	bk_snapshot_outbuf_pool_free();
	(void)bk_jpeg_encode_delete(s_snapshot.jpege_handle);
	s_snapshot.jpege_handle = NULL;
	s_snapshot.jpege_open = false;
	return ret;
}

void bk_snapshot_image_release(bk_snapshot_image_t *image)
{
	if (image == NULL || image->data == NULL) {
		return;
	}

	if (image->free_fn != NULL) {
		image->free_fn(image->data, image->free_arg);
	} else {
		bk_frame_buffer_free(image->data);
	}

	os_memset(image, 0, sizeof(*image));
}

avdk_err_t bk_snapshot_deinit(void)
{
	if (!s_snapshot.mutex_inited) {
		return AVDK_ERR_OK;
	}

	if (bk_snapshot_mutex_lock() != AVDK_ERR_OK) {
		return AVDK_ERR_GENERIC;
	}

	if (s_snapshot.active_capture != NULL) {
		bk_snapshot_mutex_unlock();
		return AVDK_ERR_BUSY;
	}

	bk_snapshot_jpege_destroy(false);
	s_snapshot.active_capture = NULL;

	rtos_unlock_mutex(&s_snapshot.mutex);
	rtos_deinit_mutex(&s_snapshot.mutex);
	os_memset(&s_snapshot, 0, sizeof(s_snapshot));
	return AVDK_ERR_OK;
}

avdk_err_t bk_snapshot_capture(bk_snapshot_config_t *config,
				   bk_snapshot_image_t *out_image)
{
	avdk_err_t ret = AVDK_ERR_OK;
	void *jpege_bond = NULL;
	bk_snapshot_capture_ctx_t capture_ctx = {0};
	isp_control_t *isp_control = NULL;
	uint32_t timeout_ms = BK_SNAPSHOT_DEFAULT_TIMEOUT_MS;
	bool h264_bond_stopped = false;

	if (config == NULL || out_image == NULL || config->source_handle == NULL ||
	    config->h264_handle == NULL || config->h264_bond == NULL ||
	    *config->h264_bond == NULL) {
		return AVDK_ERR_INVAL;
	}

	os_memset(out_image, 0, sizeof(*out_image));

	ret = bk_snapshot_mutex_lock();
	if (ret != AVDK_ERR_OK) {
		return ret;
	}

	if (config->timeout_ms > 0) {
		timeout_ms = config->timeout_ms;
	}

	isp_control = (isp_control_t *)config->source_handle;

	ret = rtos_init_semaphore(&capture_ctx.done_sem, 1);
	if (ret != BK_OK) {
		ret = AVDK_ERR_GENERIC;
		goto unlock;
	}

	s_snapshot.active_capture = &capture_ctx;
	s_snapshot.retained_outbuf = NULL;
	capture_ctx.image.width = isp_control->chn[ISP_MP_CHN_ID].chn_attr.chnFormat.width;
	capture_ctx.image.height = isp_control->chn[ISP_MP_CHN_ID].chn_attr.chnFormat.height;

	bk_flexa_isp_h264e_bond_stop(*config->h264_bond);
	*config->h264_bond = NULL;
	h264_bond_stopped = true;
	bk_snapshot_drain(BK_SNAPSHOT_H264_STOP_DRAIN_MS);

	ret = bk_snapshot_ensure_jpege(config, isp_control);
	if (ret != AVDK_ERR_OK) {
		goto restore;
	}

	ret = bk_flexa_isp_jpege_bond_start(&jpege_bond, config->source_handle, s_snapshot.jpege_handle);
	if (ret != AVDK_ERR_OK) {
		LOGE("start jpege bond failed %d\r\n", ret);
		goto restore;
	}

	LOGI("wait capture done, timeout=%ums\r\n", timeout_ms);
	ret = rtos_get_semaphore(&capture_ctx.done_sem, timeout_ms);
	if (ret != BK_OK) {
		LOGE("capture wait timeout/failed, ret=%d timeout_ms=%u capture_ok=%u\r\n",
		     ret, timeout_ms, capture_ctx.capture_ok);
	} else if (capture_ctx.capture_ok == 0) {
		LOGE("capture wait ok but encode failed, capture_ok=%u\r\n", capture_ctx.capture_ok);
	}

	s_snapshot.active_capture = NULL;
	if (ret == BK_OK && capture_ctx.capture_ok != 0) {
		s_snapshot.retained_outbuf = capture_ctx.image.data;
	} else {
		s_snapshot.retained_outbuf = NULL;
	}

	LOGW("stop jpege bond %p\r\n", jpege_bond);
	(void)bk_jpeg_encode_ioctl(s_snapshot.jpege_handle, BK_JPEG_ENCODE_IOCTL_STOP_ENCODE, NULL);
	bk_snapshot_drain(BK_SNAPSHOT_JPEG_STOP_DRAIN_MS);
	bk_flexa_isp_jpege_bond_stop(jpege_bond);
	jpege_bond = NULL;
	bk_snapshot_hw_drain();

	if (ret != BK_OK || capture_ctx.capture_ok == 0) {
		LOGW("cleanup jpege after capture failure\r\n");
		if (capture_ctx.image.data != NULL) {
			bk_snapshot_default_free(capture_ctx.image.data, NULL);
			capture_ctx.image.data = NULL;
		}
		s_snapshot.retained_outbuf = NULL;
		bk_snapshot_jpege_destroy(false);
		bk_snapshot_hw_drain();
		if (ret != BK_OK) {
			ret = AVDK_ERR_TIMEOUT;
		} else {
			ret = AVDK_ERR_HWERROR;
		}
		goto restore;
	}

	bk_snapshot_jpege_destroy(true);
	bk_snapshot_hw_drain();

	ret = bk_snapshot_restore_h264_bond(config);
	h264_bond_stopped = false;
	if (ret != AVDK_ERR_OK) {
		goto cleanup;
	}

	out_image->data = capture_ctx.image.data;
	out_image->size = capture_ctx.image.size;
	out_image->width = capture_ctx.image.width;
	out_image->height = capture_ctx.image.height;
	out_image->free_fn = capture_ctx.image.free_fn;
	out_image->free_arg = capture_ctx.image.free_arg;
	s_snapshot.retained_outbuf = NULL;
	ret = AVDK_ERR_OK;

cleanup:
	if (out_image->data == NULL && capture_ctx.image.data != NULL) {
		bk_snapshot_default_free(capture_ctx.image.data, NULL);
		capture_ctx.image.data = NULL;
	}
	s_snapshot.retained_outbuf = NULL;
	s_snapshot.active_capture = NULL;
	rtos_deinit_semaphore(&capture_ctx.done_sem);
	bk_snapshot_mutex_unlock();
	return ret;

restore:
	LOGW("restore path enter, ret=%d h264_stopped=%u jpege_bond=%p\r\n",
	     ret, h264_bond_stopped, jpege_bond);
	s_snapshot.active_capture = NULL;
	s_snapshot.retained_outbuf = NULL;
	if (jpege_bond != NULL) {
		if (s_snapshot.jpege_handle != NULL) {
			LOGW("restore: stop jpege encode\r\n");
			(void)bk_jpeg_encode_ioctl(s_snapshot.jpege_handle, BK_JPEG_ENCODE_IOCTL_STOP_ENCODE, NULL);
		}
		LOGW("restore: stop jpege bond %p\r\n", jpege_bond);
		bk_flexa_isp_jpege_bond_stop(jpege_bond);
	}
	LOGW("restore: destroy jpege\r\n");
	bk_snapshot_jpege_destroy(false);
	bk_snapshot_hw_drain();
	if (h264_bond_stopped) {
		LOGW("restore: restart h264 bond\r\n");
		avdk_err_t restore_ret = bk_snapshot_restore_h264_bond(config);

		if (restore_ret != AVDK_ERR_OK && ret == AVDK_ERR_OK) {
			ret = restore_ret;
		}
	}
	rtos_deinit_semaphore(&capture_ctx.done_sem);
	bk_snapshot_mutex_unlock();
	return ret;

unlock:
	bk_snapshot_mutex_unlock();
	return ret;
}
