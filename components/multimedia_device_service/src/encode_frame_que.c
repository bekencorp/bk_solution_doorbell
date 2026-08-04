#include <avdk_error.h>
#include <os/os.h>
#include <os/mem.h>
#include <avdk_check.h>
#include "encode_frame_que.h"
#include <components/bk_frame_buffer.h>
#include "bk_frame_queue.h"

#ifdef CONFIG_UVC_FRAME_SIZE
#define ENCODE_FRAME_SIZE (CONFIG_UVC_FRAME_SIZE)
#else
#define ENCODE_FRAME_SIZE (1024 * 200)
#endif
#define ENCODE_FRAME_COUNT 5
#define ENCODE_FRAME_CONSUMER_COUNT 1

#define TAG "encode_que"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

typedef struct {
    bk_frame_queue_handle_t queue;
    frame_buffer_t *frames;
    void *pool_alloc;
    void *pool;
    uint32_t pool_alloc_size;
    uint32_t pool_size;
    uint32_t consumer_id;
    uint8_t queue_count;
    uint8_t consumer_registered;
    uint8_t enable;
} encode_frame_queue_t;

static encode_frame_queue_t *s_encode_frame_queue = NULL;

static bool encode_frame_valid_frame(encode_frame_queue_t *queue, frame_buffer_t *frame)
{
    return frame && (frame >= queue->frames) && (frame < (queue->frames + queue->queue_count));
}

static void *encode_frame_user_data_init(uint32_t index, void *buffer, void *ctx)
{
    encode_frame_queue_t *queue = (encode_frame_queue_t *)ctx;
    frame_buffer_t *frame = NULL;

    if ((queue == NULL) || (index >= queue->queue_count)) {
        return NULL;
    }

    frame = &queue->frames[index];
    frame->frame = (uint8_t *)buffer;
    frame->size = ENCODE_FRAME_SIZE;
    return frame;
}

static avdk_err_t encode_frame_que_register_consumer(encode_frame_queue_t *queue)
{
    bk_err_t ret;

    if (queue->consumer_registered) {
        return AVDK_ERR_OK;
    }

    ret = bk_frame_queue_consumer_register(queue->queue, &queue->consumer_id);
    if (ret != BK_OK) {
        LOGE("%s, %d, register frame queue consumer failed, ret=%d\n",
             __func__, __LINE__, ret);
        return ret;
    }

    queue->consumer_registered = 1;
    return AVDK_ERR_OK;
}

static void encode_frame_que_cleanup(encode_frame_queue_t *queue)
{
    if (!queue) {
        return;
    }

    if (queue->queue) {
        (void)bk_frame_queue_destroy(queue->queue);
    }

    if (queue->pool_alloc) {
        bk_frame_buffer_free(queue->pool_alloc);
    }

    if (queue->frames) {
        os_free(queue->frames);
    }

    os_free(queue);
}

avdk_err_t encode_frame_que_init(void)
{
    avdk_err_t ret = AVDK_ERR_OK;
    bk_frame_queue_config_t config = {0};
    uint32_t pool_size;

    if (s_encode_frame_queue != NULL) {
        ret = encode_frame_que_register_consumer(s_encode_frame_queue);
        if (ret == AVDK_ERR_OK) {
            LOGW("%s, %d, encode frame queue already initialized\n", __func__, __LINE__);
        }
        return ret;
    }
    s_encode_frame_queue = (encode_frame_queue_t *)os_malloc(sizeof(encode_frame_queue_t));
    AVDK_RETURN_ON_FALSE(s_encode_frame_queue != NULL, AVDK_ERR_NOMEM, TAG,
                         "malloc encode frame queue failed");
    os_memset(s_encode_frame_queue, 0, sizeof(encode_frame_queue_t));
    s_encode_frame_queue->queue_count = ENCODE_FRAME_COUNT;

    s_encode_frame_queue->frames = (frame_buffer_t *)os_zalloc(sizeof(frame_buffer_t) * ENCODE_FRAME_COUNT);
    AVDK_GOTO_ON_FALSE(s_encode_frame_queue->frames != NULL, AVDK_ERR_NOMEM, fail,
                       TAG, "malloc frame wrappers failed");

    pool_size = bk_frame_queue_calc_pool_size(ENCODE_FRAME_SIZE, ENCODE_FRAME_COUNT);
    AVDK_GOTO_ON_FALSE(pool_size != 0, AVDK_ERR_INVAL, fail,
                       TAG, "invalid frame queue pool size");

    s_encode_frame_queue->pool_alloc_size = pool_size;
    s_encode_frame_queue->pool_alloc = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED,
                                                              s_encode_frame_queue->pool_alloc_size);
    AVDK_GOTO_ON_FALSE(s_encode_frame_queue->pool_alloc != NULL, AVDK_ERR_NOMEM, fail,
                       TAG, "malloc frame queue pool failed");

    s_encode_frame_queue->pool = s_encode_frame_queue->pool_alloc;
    s_encode_frame_queue->pool_size = pool_size;

    config.block_size = ENCODE_FRAME_SIZE;
    config.block_count = ENCODE_FRAME_COUNT;
    config.consumer_count = ENCODE_FRAME_CONSUMER_COUNT;
    config.pool = s_encode_frame_queue->pool;
    config.pool_size = pool_size;
    config.user_data_cb = encode_frame_user_data_init;
    config.user_data_ctx = s_encode_frame_queue;
    AVDK_GOTO_ON_ERROR(bk_frame_queue_create(&config, &s_encode_frame_queue->queue),
                       fail, TAG, "create frame queue failed");

    AVDK_GOTO_ON_ERROR(encode_frame_que_register_consumer(s_encode_frame_queue),
                       fail, TAG, "register frame queue consumer failed");

    s_encode_frame_queue->enable = 1;
    return ret;

fail:
    encode_frame_que_cleanup(s_encode_frame_queue);
    s_encode_frame_queue = NULL;
    return ret;
}

avdk_err_t encode_frame_que_deinit(void)
{
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;
    AVDK_RETURN_ON_FALSE((encode_frame_queue != NULL) && (encode_frame_queue->enable != 0),
                         AVDK_ERR_OK, TAG, "encode frame queue is not initialized");

    encode_frame_queue->enable = 0;
    encode_frame_que_cleanup(encode_frame_queue);
    s_encode_frame_queue = NULL;
    return AVDK_ERR_OK;
}

avdk_err_t encode_ready_frame_que_push(frame_buffer_t *frame)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;

    AVDK_RETURN_ON_FALSE(encode_frame_queue != NULL, ret, TAG,
                         "encode frame queue is not initialized");
    AVDK_RETURN_ON_FALSE(encode_frame_queue->enable != 0, ret, TAG,
                         "encode frame queue is not enabled");
    AVDK_RETURN_ON_FALSE(encode_frame_valid_frame(encode_frame_queue, frame),
                         AVDK_ERR_INVAL, TAG, "invalid encode frame");

    ret = bk_frame_queue_producer_commit(encode_frame_queue->queue, frame->frame,
                                         frame->length, frame);
    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, commit encode frame failed, ret=%d\n", __func__, __LINE__, ret);
    }

    return ret;
}

avdk_err_t encode_ready_frame_que_wakeup(void)
{
    avdk_err_t ret = AVDK_ERR_OK;
    bk_err_t queue_ret;
    frame_buffer_t *frame = NULL;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;

    AVDK_RETURN_ON_FALSE(encode_frame_queue != NULL, AVDK_ERR_OK, TAG,
                         "encode frame queue is not initialized");
    AVDK_RETURN_ON_FALSE(encode_frame_queue->enable != 0, AVDK_ERR_OK, TAG,
                         "encode frame queue is not enabled");

    if (encode_frame_queue->consumer_registered) {
        queue_ret = bk_frame_queue_consumer_unregister(encode_frame_queue->queue,
                                                       encode_frame_queue->consumer_id);
        if ((queue_ret == BK_OK) || (queue_ret == BK_ERR_NOT_FOUND)) {
            encode_frame_queue->consumer_registered = 0;
        }
    }
    (void)frame;

    return ret;
}

avdk_err_t encode_free_frame_que_push(frame_buffer_t *frame)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;

    AVDK_RETURN_ON_FALSE(encode_frame_queue != NULL, ret, TAG,
                         "encode frame queue is not initialized");
    AVDK_RETURN_ON_FALSE(encode_frame_queue->enable != 0, ret, TAG,
                         "encode frame queue is not enabled");
    AVDK_RETURN_ON_FALSE(encode_frame_valid_frame(encode_frame_queue, frame),
                         AVDK_ERR_INVAL, TAG, "invalid encode frame");

    ret = bk_frame_queue_producer_drop(encode_frame_queue->queue, frame->frame);
    if (ret == BK_ERR_STATE) {
        ret = bk_frame_queue_consumer_release(encode_frame_queue->queue,
                                              encode_frame_queue->consumer_id,
                                              frame->frame);
    }

    if (ret != AVDK_ERR_OK) {
        LOGE("%s, %d, release encode frame failed, ret=%d\n", __func__, __LINE__, ret);
    }

    return ret;
}

frame_buffer_t *encode_ready_frame_que_pop(uint32_t timeout)
{
    frame_buffer_t *frame = NULL;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;
    void *payload = NULL;
    void *user_data = NULL;
    uint32_t length = 0;
    avdk_err_t ret;

    AVDK_RETURN_ON_FALSE(encode_frame_queue != NULL, NULL, TAG,
                         "encode frame queue is not initialized");
    AVDK_RETURN_ON_FALSE(encode_frame_queue->enable != 0, NULL, TAG,
                         "encode frame queue is not enabled");
    AVDK_RETURN_ON_FALSE(encode_frame_queue->consumer_registered != 0, NULL, TAG,
                         "encode frame queue consumer is not registered");

    ret = bk_frame_queue_consumer_acquire(encode_frame_queue->queue,
                                          encode_frame_queue->consumer_id,
                                          &payload,
                                          &length,
                                          &user_data,
                                          timeout);
    if (ret != AVDK_ERR_OK) {
        return NULL;
    }

    frame = (frame_buffer_t *)user_data;
    if (frame == NULL) {
        LOGE("%s, %d, frame user data is NULL\n", __func__, __LINE__);
        bk_frame_queue_consumer_release(encode_frame_queue->queue,
                                        encode_frame_queue->consumer_id,
                                        payload);
        return NULL;
    }

    frame->length = length;
    return frame;
}

frame_buffer_t *encode_free_frame_que_pop(void)
{
    frame_buffer_t *frame = NULL;
    encode_frame_queue_t *encode_frame_queue = s_encode_frame_queue;
    void *payload = NULL;
    void *user_data = NULL;
    avdk_err_t ret;

    AVDK_RETURN_ON_FALSE(encode_frame_queue != NULL, NULL, TAG,
                         "encode frame queue is not initialized");
    AVDK_RETURN_ON_FALSE(encode_frame_queue->enable != 0, NULL, TAG,
                         "encode frame queue is not enabled");

    ret = bk_frame_queue_producer_acquire(encode_frame_queue->queue,
                                          &payload,
                                          &user_data,
                                          BEKEN_NO_WAIT);
    if (ret != AVDK_ERR_OK) {
        return NULL;
    }

    frame = (frame_buffer_t *)user_data;
    if (frame == NULL) {
        LOGE("%s, %d, free frame user data is NULL\n", __func__, __LINE__);
        bk_frame_queue_producer_drop(encode_frame_queue->queue, payload);
        return NULL;
    }

    frame->length = 0;
    return frame;
}