#include <avdk_error.h>
#include <os/os.h>
#include <os/mem.h>
#include "encode_frame_que.h"
#include <components/bk_frame_buffer.h>

#ifdef CONFIG_UVC_FRAME_SIZE
#define ENCODE_FRAME_SIZE (CONFIG_UVC_FRAME_SIZE)
#else
#define ENCODE_FRAME_SIZE (1024 * 200)
#endif
#define ENCODE_FRAME_COUNT 5

#define TAG "encode_que"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

typedef struct {
    beken_queue_t ready_queue;
    beken_queue_t free_queue;
    beken_mutex_t mutex;
    uint8_t queue_count;
    uint8_t enable;
} encode_frame_queue_t;

static encode_frame_queue_t *s_encode_frame_queue = NULL;

avdk_err_t encode_frame_que_init(void)
{
    avdk_err_t ret = AVDK_ERR_OK;
    if (s_encode_frame_queue != NULL) {
        LOGW("%s, %d, encode frame queue already initialized\n", __func__, __LINE__);
        return AVDK_ERR_OK;
    }
    s_encode_frame_queue = (encode_frame_queue_t *)os_malloc(sizeof(encode_frame_queue_t));
    if (s_encode_frame_queue == NULL) {
        LOGE("%s, %d, malloc encode frame queue failed\n", __func__, __LINE__);
        return AVDK_ERR_NOMEM;
    }
    os_memset(s_encode_frame_queue, 0, sizeof(encode_frame_queue_t));
    s_encode_frame_queue->queue_count = ENCODE_FRAME_COUNT;
    ret = rtos_init_queue(&s_encode_frame_queue->ready_queue, "encode_frame_ready_queue", sizeof(void *), ENCODE_FRAME_COUNT);
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, init encode frame ready queue failed\n", __func__, __LINE__);
        return ret;
    }
    ret = rtos_init_queue(&s_encode_frame_queue->free_queue, "encode_frame_free_queue", sizeof(void *), ENCODE_FRAME_COUNT);
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, init encode frame free queue failed\n", __func__, __LINE__);
        return ret;
    }

    ret = rtos_init_mutex(&s_encode_frame_queue->mutex);
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, init encode frame mutex failed\n", __func__, __LINE__);
        return ret;
    }

    for (uint8_t i = 0; i < ENCODE_FRAME_COUNT; i++) {
        frame_buffer_t *frame = (frame_buffer_t *)os_malloc(sizeof(frame_buffer_t));
        if (frame == NULL) {
            LOGE("%s, %d, malloc encode frame failed\n", __func__, __LINE__);
            return AVDK_ERR_NOMEM;
        }
        os_memset(frame, 0, sizeof(frame_buffer_t));
        uint8_t *frame_data = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, ENCODE_FRAME_SIZE);
        if (frame_data == NULL) {
            LOGE("%s, %d, malloc encode frame data failed\n", __func__, __LINE__);
            os_free(frame);
            return AVDK_ERR_NOMEM;
        }

        frame->frame = frame_data;
        frame->size = ENCODE_FRAME_SIZE;
        LOGD("%s, %d, frame:%p, encode frame:%p, size:%d\n", __func__, __LINE__, frame, frame->frame, frame->size);

        /*
         * Queue item size is sizeof(void *). rtos_push_to_queue() copies message_size bytes
         * from the address "message" points to, so we must pass &frame to push the pointer value.
         */
        ret = rtos_push_to_queue(&s_encode_frame_queue->free_queue, &frame, BEKEN_NO_WAIT);
        if (ret != AVDK_ERR_OK) {
            LOGE("%s, %d, push encode frame to free queue failed\n", __func__, __LINE__);
            return ret;
        }
    }
    s_encode_frame_queue->enable = 1;
    return ret;
}

avdk_err_t encode_frame_que_deinit(void)
{
    avdk_err_t ret = AVDK_ERR_OK;
    frame_buffer_t *frame = NULL;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;
    if (encode_frame_queue == NULL || encode_frame_queue->enable == 0) {
        LOGE("%s, %d, encode frame queue is not initialized\n", __func__, __LINE__);
        return AVDK_ERR_OK;
    }

    rtos_lock_mutex(&encode_frame_queue->mutex);

    encode_frame_queue->enable = 0;

    while (!rtos_is_queue_empty(&encode_frame_queue->free_queue)) {
        ret = rtos_pop_from_queue(&encode_frame_queue->free_queue, &frame, BEKEN_NO_WAIT);
        if (ret != AVDK_ERR_OK) {
            LOGE("%s, %d, pop encode frame from free queue failed\n", __func__, __LINE__);
            break;
        }
        bk_frame_buffer_free(frame->frame);
        os_free(frame);
    }

    while (!rtos_is_queue_empty(&encode_frame_queue->ready_queue)) {
        ret = rtos_pop_from_queue(&encode_frame_queue->ready_queue, &frame, BEKEN_NO_WAIT);
        if (ret != AVDK_ERR_OK) {
            LOGE("%s, %d, pop encode frame from ready queue failed\n", __func__, __LINE__);
            break;
        }
        bk_frame_buffer_free(frame->frame);
        os_free(frame);
    }

    rtos_deinit_queue(&encode_frame_queue->ready_queue);
    rtos_deinit_queue(&encode_frame_queue->free_queue);
    rtos_unlock_mutex(&encode_frame_queue->mutex);
    rtos_deinit_mutex(&encode_frame_queue->mutex);
    os_free(encode_frame_queue);
    s_encode_frame_queue = NULL;
    return AVDK_ERR_OK;
}

avdk_err_t encode_ready_frame_que_push(frame_buffer_t *frame)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;
    if (encode_frame_queue == NULL) {
        LOGE("%s, %d, encode frame queue is not initialized\n", __func__, __LINE__);
        return ret;
    }

    rtos_lock_mutex(&encode_frame_queue->mutex);
    if (encode_frame_queue->enable == 0) {
        LOGE("%s, %d, encode frame queue is not enabled\n", __func__, __LINE__);
        rtos_unlock_mutex(&encode_frame_queue->mutex);
        return ret;
    }

    /* Queue item size is sizeof(void *), push pointer value via &frame. */
    ret = rtos_push_to_queue(&encode_frame_queue->ready_queue, &frame, BEKEN_NO_WAIT);
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, push encode frame to ready queue failed\n", __func__, __LINE__);
    }

    rtos_unlock_mutex(&encode_frame_queue->mutex);

    return ret;
}

avdk_err_t encode_free_frame_que_push(frame_buffer_t *frame)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;
    if (encode_frame_queue == NULL) {
        LOGE("%s, %d, encode frame queue is not initialized\n", __func__, __LINE__);
        return ret;
    }

    rtos_lock_mutex(&encode_frame_queue->mutex);
    if (encode_frame_queue->enable == 0) {
        LOGE("%s, %d, encode frame queue is not enabled\n", __func__, __LINE__);
        rtos_unlock_mutex(&encode_frame_queue->mutex);
        return ret;
    }

    /* Queue item size is sizeof(void *), push pointer value via &frame. */
    ret = rtos_push_to_queue(&encode_frame_queue->free_queue, &frame, BEKEN_NO_WAIT);
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, push encode frame to free queue failed\n", __func__, __LINE__);
    }

    rtos_unlock_mutex(&encode_frame_queue->mutex);

    return ret;
}

frame_buffer_t *encode_ready_frame_que_pop(uint32_t timeout)
{
    frame_buffer_t *frame = NULL;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;
    if (encode_frame_queue == NULL) {
        LOGE("%s, %d, encode frame queue is not initialized\n", __func__, __LINE__);
        return NULL;
    }

    rtos_lock_mutex(&encode_frame_queue->mutex);
    if (encode_frame_queue->enable == 0) {
        LOGE("%s, %d, encode frame queue is not enabled\n", __func__, __LINE__);
        rtos_unlock_mutex(&encode_frame_queue->mutex);
        return NULL;
    }

    rtos_unlock_mutex(&encode_frame_queue->mutex);

    avdk_err_t ret = rtos_pop_from_queue(&encode_frame_queue->ready_queue, &frame, timeout);
    if (ret != AVDK_ERR_OK) {
        LOGV("%s, %d, pop encode frame from ready queue failed\n", __func__, __LINE__);
    }

    return frame;
}

frame_buffer_t *encode_free_frame_que_pop(void)
{
    frame_buffer_t *frame = NULL;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;
    if (encode_frame_queue == NULL) {
        LOGE("%s, %d, encode frame queue is not initialized\n", __func__, __LINE__);
        return NULL;
    }

    rtos_lock_mutex(&encode_frame_queue->mutex);
    if (encode_frame_queue->enable == 0) {
        LOGE("%s, %d, encode frame queue is not enabled\n", __func__, __LINE__);
        rtos_unlock_mutex(&encode_frame_queue->mutex);
        return NULL;
    }

    avdk_err_t ret = rtos_pop_from_queue(&encode_frame_queue->free_queue, &frame, BEKEN_NO_WAIT);
    if (ret != AVDK_ERR_OK) {
#if 1
        rtos_pop_from_queue(&encode_frame_queue->ready_queue, &frame, BEKEN_NO_WAIT);
#endif
    }

    rtos_unlock_mutex(&encode_frame_queue->mutex);

    return frame;
}