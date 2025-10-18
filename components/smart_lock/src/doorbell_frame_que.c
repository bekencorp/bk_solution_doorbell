// Copyright 2024-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <os/os.h>
#include <os/mem.h>
#include <driver/int.h>
#include <components/log.h>
#include "avdk_crc.h"

#include "frame_buffer.h"
#include "doorbell_frame_que.h"

#define TAG "frame_buffer"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define FB_LIST_MAX_COUNT (3)
#define MJPEG_MAX_FRAME_COUNT     (4)
#define H264_MAX_FRAME_COUNT      (6)
#define YUV_MAX_FRAME_COUNT       (3)

typedef struct
{
    uint8_t count;
    image_format_t format;
    beken_queue_t free_que;
    beken_queue_t ready_que;
} frame_queue_t;

typedef struct
{
    uint32_t frame;
} frame_msg_t;

frame_queue_t frame_queue[FB_LIST_MAX_COUNT];

static int get_frame_buffer_list_index(image_format_t format)
{
    int index = -1;
    switch (format)
    {
        case IMAGE_MJPEG:
            index = 0;
            break;
        case IMAGE_H264:
            index = 1;
            break;
        case IMAGE_YUV:
            index = 2;
            break;
        default:
            break;
    }

    return index;
}

/**
 * @brief 初始化frame_queue数据结构
 * 
 * @param format 图像格式
 * @return bk_err_t 初始化结果
 */
bk_err_t frame_queue_init(image_format_t format)
{
    bk_err_t ret = BK_FAIL;
    int index = -1;
    uint8_t frame_count = 0;
    
    // 获取对应格式的索引和帧数量
    switch (format)
    {
        case IMAGE_MJPEG:
            index = 0;
            frame_count = MJPEG_MAX_FRAME_COUNT;
            break;
        case IMAGE_H264:
            index = 1;
            frame_count = H264_MAX_FRAME_COUNT;
            break;
        case IMAGE_YUV:
            index = 2;
            frame_count = YUV_MAX_FRAME_COUNT;
            break;
        default:
            break;
    }

    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return ret;
    }

    // 检查是否已经初始化
    if (frame_queue[index].count > 0)
    {
        ret = BK_OK;
        LOGD("%s, %d alredy init:%d\n", __func__, __LINE__, format);
        return ret;
    }

    // 初始化frame_queue结构
    frame_queue[index].count = frame_count;
    frame_queue[index].format = format;

    // 初始化队列
    // 注意：这里需要根据实际的beken_queue_t定义来实现队列初始化
    // 假设beken_queue_t有相应的初始化函数
    ret = rtos_init_queue(&frame_queue[index].free_que, "free_que", sizeof(frame_msg_t), frame_count);
    if (ret != BK_OK)
    {
        LOGD("%s, %d free_que init fail:%d\n", __func__, __LINE__, ret);
        return ret;
    }

    for (int i = 0; i < frame_count; i++)
    {
        frame_msg_t msg;
        msg.frame = 0;
        rtos_push_to_queue(&frame_queue[index].free_que, &msg, 0);
    }

    ret = rtos_init_queue(&frame_queue[index].ready_que, "ready_que", sizeof(frame_msg_t), frame_count);
    if (ret != BK_OK)
    {
        // 如果ready_que初始化失败，需要释放已经初始化的free_que
        rtos_deinit_queue(&frame_queue[index].free_que);
        LOGD("%s, %d ready_que init fail:%d\n", __func__, __LINE__, ret);
        return ret;
    }

    return ret;
}

/**
 * @brief 初始化所有frame_queue数据结构
 * 
 * @return bk_err_t 初始化结果
 */
bk_err_t frame_queue_init_all(void)
{
    // 初始化所有格式的frame_queue
    if (frame_queue_init(IMAGE_MJPEG) != BK_OK)
    {
        return BK_FAIL;
    }

    if (frame_queue_init(IMAGE_H264) != BK_OK)
    {
        return BK_FAIL;
    }

    if (frame_queue_init(IMAGE_YUV) != BK_OK)
    {
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief 释放frame_queue数据结构
 * 
 * @param format 图像格式
 * @return bk_err_t 释放结果
 */
bk_err_t frame_queue_deinit(image_format_t format)
{
    int index = -1;
    
    // 获取对应格式的索引
    index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return BK_FAIL;
    }

    // 检查是否已经初始化
    if (frame_queue[index].count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, format);
        return BK_OK;
    }

    // 释放队列资源
    // 遍历free_que队列，释放其中的消息
    frame_msg_t msg;
    while (rtos_pop_from_queue(&frame_queue[index].free_que, &msg, 0) == BK_OK)
    {
        if (msg.frame)
        {
            frame_buffer_t *frame = (frame_buffer_t *)msg.frame;
            // 根据图像格式选择合适的释放函数
            switch (format)
            {
                case IMAGE_MJPEG:
                case IMAGE_H264:
                    frame_buffer_encode_free(frame);
                    break;
                case IMAGE_YUV:
                    frame_buffer_display_free(frame);
                    break;
                default:
                    // 如果格式未知，使用默认释放函数
                    LOGW("%s, %d unknown format:%d\n", __func__, __LINE__, format);
                    break;
            }
        }
    }

    // 遍历ready_que队列，释放其中的消息
    while (rtos_pop_from_queue(&frame_queue[index].ready_que, &msg, 0) == BK_OK)
    {
        if (msg.frame)
        {
            frame_buffer_t *frame = (frame_buffer_t *)msg.frame;
            // 根据图像格式选择合适的释放函数
            switch (format)
            {
                case IMAGE_MJPEG:
                case IMAGE_H264:
                    frame_buffer_encode_free(frame);
                    break;
                case IMAGE_YUV:
                    frame_buffer_display_free(frame);
                    break;
                default:
                    // 如果格式未知，使用默认释放函数
                    LOGW("%s, %d unknown format:%d\n", __func__, __LINE__, format);
                    break;
            }
        }
    }

    rtos_deinit_queue(&frame_queue[index].free_que);
    rtos_deinit_queue(&frame_queue[index].ready_que);

    // 重置frame_queue结构
    frame_queue[index].count = 0;
    frame_queue[index].format = IMAGE_UNKNOW; // 假设存在IMAGE_NONE枚举值

    return BK_OK;
}

/**
 * @brief 释放所有frame_queue数据结构
 * 
 * @return bk_err_t 释放结果
 */
bk_err_t frame_queue_deinit_all(void)
{
    // 释放所有格式的frame_queue
    frame_queue_deinit(IMAGE_MJPEG);
    frame_queue_deinit(IMAGE_H264);
    frame_queue_deinit(IMAGE_YUV);

    return BK_OK;
}

/**
 * @brief 清空所有frame_queue的ready队列
 * 
 * @return bk_err_t 操作结果
 */
bk_err_t frame_queue_clear_all(void)
{
    int index = 0;
    frame_msg_t msg;

    // 遍历所有frame_queue
    for (index = 0; index < FB_LIST_MAX_COUNT; index++)
    {
        // 检查是否已经初始化
        if (frame_queue[index].count == 0)
        {
            continue;
        }

        // 遍历ready_que队列，释放其中的所有frame
        while (rtos_pop_from_queue(&frame_queue[index].ready_que, &msg, 0) == BK_OK)
        {
            if (msg.frame)
            {
                frame_buffer_t *frame = (frame_buffer_t *)msg.frame;
                image_format_t format = frame_queue[index].format;

                // 直接调用frame_queue_free释放frame
                frame_queue_free(format, frame);
            }
        }

        LOGI("%s, clear ready queue for format:%d\n", __func__, frame_queue[index].format);
    }

    return BK_OK;
}

/**
 * @brief 从frame_queue中申请一个frame_buffer
 * 
 * @param format 图像格式
 * @param size 申请的frame大小
 * @return frame_buffer_t* 申请到的frame_buffer，如果申请失败则返回NULL
 */
frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size)
{
    int index = -1;
    frame_msg_t msg;
    frame_buffer_t *frame = NULL;
    
    // 获取对应格式的索引
    index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return NULL;
    }

    // 检查是否已经初始化
    if (frame_queue[index].count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, format);
        return NULL;
    }

    // 从free_que中获取消息
    if (rtos_pop_from_queue(&frame_queue[index].free_que, &msg, 0) == BK_OK)
    {
        // 根据图像格式选择合适的申请函数
        switch (format)
        {
            case IMAGE_MJPEG:
            case IMAGE_H264:
                frame = frame_buffer_encode_malloc(size);
                break;
            case IMAGE_YUV:
                frame = frame_buffer_display_malloc(size);
                break;
            default:
                // 如果格式未知，返回NULL
                LOGW("%s, %d unknown format:%d\n", __func__, __LINE__, format);
                // 将消息放回队列
                rtos_push_to_queue(&frame_queue[index].free_que, &msg, 0);
                return NULL;
        }

        // 如果申请frame失败，将消息放回队列
        if (frame == NULL)
        {
            LOGW("%s, %d malloc frame fail\n", __func__, __LINE__);
            // 将消息放回队列
            rtos_push_to_queue(&frame_queue[index].free_que, &msg, 0);
        }
    }

    // 如果是从JPEG或者YUV格式，可以从ready_que中取一个
    if (frame == NULL && (format == IMAGE_MJPEG || format == IMAGE_YUV))
    {
        if (rtos_pop_from_queue(&frame_queue[index].ready_que, &msg, 0) == BK_OK)
        {
            frame = (frame_buffer_t *)msg.frame;
        }
    }

    if (frame == NULL)
    {
        return NULL;
    }

    frame->type = 0;
    frame->fmt = 0;
    frame->crc = 0;
    frame->timestamp = 0;
    frame->width = 0;
    frame->height = 0;
    frame->length = 0;
    frame->sequence = 0;
    frame->h264_type = 0;

    return frame;
}

/**
 * @brief 从frame_queue的ready队列中获取一个frame_buffer
 * 
 * @param format 图像格式
 * @param timeout 超时时间（毫秒）
 * @return frame_buffer_t* 获取到的frame_buffer，如果获取失败则返回NULL
 */
frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout)
{
    int index = -1;
    frame_msg_t msg;
    frame_buffer_t *frame = NULL;

    // 获取对应格式的索引
    index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return NULL;
    }

    // 检查是否已经初始化
    if (frame_queue[index].count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, format);
        return NULL;
    }

    // 从ready_que中获取消息
    if (rtos_pop_from_queue(&frame_queue[index].ready_que, &msg, timeout) == BK_OK)
    {
        frame = (frame_buffer_t *)msg.frame;
        return frame;
    }

    // 如果获取失败，则返回空
    return NULL;
}

/**
 * @brief 将frame_buffer放回ready_que队列
 * 
 * @param format 图像格式
 * @param frame 要放回队列的frame_buffer
 * @return bk_err_t 操作结果
 */
bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame)
{
    int index = -1;
    frame_msg_t msg;

    // 获取对应格式的索引
    index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return BK_FAIL;
    }

    // 检查是否已经初始化
    if (frame_queue[index].count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, format);
        return BK_FAIL;
    }

    // 构造消息
    msg.frame = (uint32_t)frame;

    // 将消息放入ready_que队列
    if (rtos_push_to_queue(&frame_queue[index].ready_que, &msg, 0) != BK_OK)
    {
        LOGE("%s, %d rtos_push_to_queue fail\n", __func__, __LINE__);
        // 根据图像格式选择合适的释放函数
        frame_queue_free(format, frame);
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief 根据图像格式释放frame_buffer，并将消息发送到free_que
 * 
 * @param format 图像格式
 * @param frame 要释放的frame_buffer
 * @return void
 */
void frame_queue_free(image_format_t format, frame_buffer_t *frame)
{
    int index = -1;
    frame_msg_t msg;

    // 获取对应格式的索引
    index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return;
    }

    // 检查是否已经初始化
    if (frame_queue[index].count == 0)
    {
        LOGD("%s, %d not init:%d\n", __func__, __LINE__, format);
        return;
    }

    // 根据图像格式选择合适的释放函数
    switch (format)
    {
        case IMAGE_MJPEG:
        case IMAGE_H264:
            frame_buffer_encode_free(frame);
            break;
        case IMAGE_YUV:
            frame_buffer_display_free(frame);
            break;
        default:
            // 如果格式未知，使用默认释放函数
            LOGW("%s, %d unknown format:%d\n", __func__, __LINE__, format);
            return;
    }

    // 构造消息
    msg.frame = 0; // 释放frame后，消息中的frame指针应为NULL

    // 将消息放入free_que队列
    if (rtos_push_to_queue(&frame_queue[index].free_que, &msg, 0) != BK_OK)
    {
        LOGE("%s, %d rtos_push_to_queue fail\n", __func__, __LINE__);
    }
}
