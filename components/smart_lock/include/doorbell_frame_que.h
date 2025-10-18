#pragma once

#include <components/media_types.h>


/**
 * @brief 初始化所有frame_queue数据结构
 * 
 * @return bk_err_t 初始化结果
 */
bk_err_t frame_queue_init_all(void);

/**
 * @brief 释放所有frame_queue数据结构
 * 
 * @return bk_err_t 释放结果
 */
bk_err_t frame_queue_deinit_all(void);

/**
 * @brief 清空所有frame_queue的ready队列
 * 
 * @return bk_err_t 操作结果
 */
bk_err_t frame_queue_clear_all(void);

/**
 * @brief 分配一个frame buffer
 * 
 * @param format 图像格式
 * @param size 请求的buffer大小
 * @return frame_buffer_t* 分配的frame buffer指针，失败时返回NULL
 */
frame_buffer_t *frame_queue_malloc(image_format_t format, uint32_t size);

/**
 * @brief 从frame_queue的ready队列中获取一个frame_buffer
 * 
 * @param format 图像格式
 * @param timeout 超时时间（毫秒）
 * @return frame_buffer_t* 获取到的frame_buffer，如果获取失败则返回NULL
 */
frame_buffer_t *frame_queue_get_frame(image_format_t format, uint32_t timeout);

/**
 * @brief 将frame_buffer放回ready_que队列
 * 
 * @param format 图像格式
 * @param frame 要放回队列的frame_buffer
 * @return bk_err_t 操作结果
 */
bk_err_t frame_queue_complete(image_format_t format, frame_buffer_t *frame);

/**
 * @brief 根据图像格式释放frame_buffer，并将消息发送到free_que
 * 
 * @param format 图像格式
 * @param frame 要释放的frame_buffer
 */
void frame_queue_free(image_format_t format, frame_buffer_t *frame);
