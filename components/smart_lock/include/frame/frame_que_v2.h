#pragma once

#include <components/media_types.h>

// Consumer type definitions
#define CONSUMER_TRANSMISSION   (1 << 0)  // Video transmission task
#define CONSUMER_DECODER        (1 << 1)  // Decoder task
#define CONSUMER_STORAGE        (1 << 2)  // Storage task
#define CONSUMER_RECOGNITION    (1 << 3)  // Recognition task
#define CONSUMER_CUSTOM_1       (1 << 4)  // Custom task 1
#define CONSUMER_CUSTOM_2       (1 << 5)  // Custom task 2
#define CONSUMER_CUSTOM_3       (1 << 6)  // Custom task 3
#define CONSUMER_CUSTOM_4       (1 << 7)  // Custom task 4

/**
 * @brief Initialize all frame_queue data structures (V2 version)
 *
 * @return bk_err_t Initialization result
 */
bk_err_t frame_queue_v2_init_all(void);

/**
 * @brief Deinitialize all frame_queue data structures (V2 version)
 *
 * @return bk_err_t Deinitialization result
 */
bk_err_t frame_queue_v2_deinit_all(void);

/**
 * @brief Clear ready queues of all frame_queues (V2 version)
 *
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_v2_clear_all(void);

/**
 * @brief Register consumer
 *
 * @param format Image format
 * @param consumer_id Consumer ID (CONSUMER_XXX)
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_v2_register_consumer(image_format_t format, uint32_t consumer_id);

/**
 * @brief Unregister consumer
 *
 * @param format Image format
 * @param consumer_id Consumer ID
 * @return bk_err_t Operation result
 *
 * @note After unregistration, consumer will no longer need to access new frames, held frames will be auto-updated
 */
bk_err_t frame_queue_v2_unregister_consumer(image_format_t format, uint32_t consumer_id);

/**
 * @brief Allocate a frame buffer (for producer use)
 *
 * @param format Image format
 * @param size Requested buffer size
 * @return frame_buffer_t* Allocated frame buffer pointer, NULL on failure
 *
 * @note After producer fills data, must call frame_queue_v2_complete to put frame into ready queue
 */
frame_buffer_t *frame_queue_v2_malloc(image_format_t format, uint32_t size);

/**
 * @brief Put filled frame into ready queue (for producer use)
 *
 * @param format Image format
 * @param frame frame_buffer to put back into queue
 * @return bk_err_t Operation result
 *
 * @note After calling this function, all registered consumers can access this frame
 */
bk_err_t frame_queue_v2_complete(image_format_t format, frame_buffer_t *frame);

/**
 * @brief Cancel allocated but failed frame (for producer use)
 *
 * @param format Image format
 * @param frame frame_buffer to cancel
 * @return bk_err_t Operation result
 *
 * @note Call this function to safely release resources when producer encounters error after malloc, avoiding memory leak and double free
 */
bk_err_t frame_queue_v2_cancel(image_format_t format, frame_buffer_t *frame);

/**
 * @brief Consumer get frame (supports multiple consumers accessing same frame)
 *
 * @param format Image format
 * @param consumer_id Consumer ID
 * @param timeout Timeout in milliseconds, BEKEN_WAIT_FOREVER for infinite wait
 * @return frame_buffer_t* Retrieved frame_buffer, NULL if retrieval fails
 *
 * @note Multiple consumers can access same frame simultaneously, each consumer must call frame_queue_v2_release_frame after use
 */
frame_buffer_t *frame_queue_v2_get_frame(image_format_t format, uint32_t consumer_id, uint32_t timeout);

/**
 * @brief Consumer release frame
 *
 * @param format Image format
 * @param consumer_id Consumer ID
 * @param frame frame_buffer to release
 *
 * @note Frame will only be truly recycled to free_list after all consumers that need to access it have released it
 */
void frame_queue_v2_release_frame(image_format_t format, uint32_t consumer_id, frame_buffer_t *frame);

/**
 * @brief Get queue statistics
 *
 * @param format Image format
 * @param free_count Output: number of free frames
 * @param ready_count Output: number of ready frames
 * @param total_malloc Output: total malloc count
 * @param total_complete Output: total complete count
 * @param total_free Output: total free count
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_v2_get_stats(image_format_t format,
                                  uint32_t *free_count,
                                  uint32_t *ready_count,
                                  uint32_t *total_malloc,
                                  uint32_t *total_complete,
                                  uint32_t *total_free);


