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

/**
 * @file doorbell_frame_que_v2.c
 * @brief Multi-consumer frame queue management system V2
 *
 * Key Features:
 * 1. Support multiple consumers accessing the same frame simultaneously (reference counting)
 * 2. Slow consumer protection (automatic frame dropping, no blocking of fast consumers)
 * 3. Dynamic consumer registration/unregistration
 * 4. Doubly-linked list management (replacing original queues)
 * 5. Frame buffer reuse
 */

#include <os/os.h>
#include <os/mem.h>
#include <driver/int.h>
#include <components/log.h>
#include "avdk_crc.h"

#include "frame_buffer.h"
#include "frame/frame_que_v2.h"

#define TAG "frame_que_v2"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

// Debug log switch: set to 1 to enable detailed debug logs, 0 to disable (disabled by default)
#define FRAME_QUEUE_V2_DEBUG_LOG 0

#if FRAME_QUEUE_V2_DEBUG_LOG
#define LOG_DEBUG(...) LOGI(__VA_ARGS__)
#else
#define LOG_DEBUG(...) do {} while(0)
#endif

#define FB_LIST_MAX_COUNT (3)
#define MJPEG_MAX_FRAME_COUNT     (4)
#define H264_MAX_FRAME_COUNT      (6)
#define YUV_MAX_FRAME_COUNT       (3)


/**
 * @brief Frame node structure - frame wrapper with reference counting and access mask
 */
typedef struct frame_node
{
    frame_buffer_t *frame;              // Actual frame buffer
    struct frame_node *next;            // Next node in linked list
    struct frame_node *prev;            // Previous node in linked list

    uint32_t ref_count;                 // Reference count
    uint32_t consumer_mask;             // Mask of consumers that need to access this frame
    uint32_t accessed_mask;             // Mask of consumers that have accessed this frame
    uint64_t timestamp;                 // Frame timestamp (for timeout detection)
    uint64_t create_time;               // Frame creation time

    uint8_t in_use;                     // Whether occupied by producer (after malloc, before complete)
} frame_node_t;

/**
 * @brief Doubly-linked list structure
 */
typedef struct
{
    frame_node_t *head;
    frame_node_t *tail;
    uint32_t count;
} frame_list_t;

/**
 * @brief Consumer information structure
 */
typedef struct
{
    uint32_t consumer_id;               // Consumer ID (mask bit)
    uint32_t get_count;                 // Frame get count (statistics)
    uint32_t release_count;             // Frame release count (statistics)
    uint8_t enabled;                    // Whether enabled
} consumer_info_t;

/**
 * @brief Frame queue management structure
 */
typedef struct
{
    uint8_t count;                      // Total frame count
    image_format_t format;              // Image format
    frame_list_t free_list;             // Free frame linked list
    frame_list_t ready_list;            // Ready frame linked list

    uint32_t active_consumers;          // Active consumer mask
    consumer_info_t consumers[8];       // Consumer information array (max 8)
    beken_semaphore_t frame_ready;      // Semaphore to wake consumers when new frame is ready

    uint32_t total_malloc;              // Statistics: total malloc count
    uint32_t total_complete;            // Statistics: total complete count
    uint32_t total_free;                // Statistics: total free count
} frame_queue_v2_t;

static frame_queue_v2_t frame_queue_v2[FB_LIST_MAX_COUNT];

// Global spinlock for frame queue operations (protects all frame queue data)
// In SMP systems: disable interrupt + spinlock
// In single-core systems: disable interrupt only
#ifdef CONFIG_FREERTOS_SMP
static SPINLOCK_SECTION volatile spinlock_t fb_spin_lock = SPIN_LOCK_INIT;
#endif

/**
 * @brief Enter critical section (interrupt-safe, SMP-safe)
 *
 * This function is used to protect all frame queue operations.
 * Works in both interrupt context and task context.
 *
 * @return uint32_t Interrupt flags to be restored later
 */
static inline uint32_t fb_enter_critical()
{
    uint32_t flags = rtos_disable_int();

#ifdef CONFIG_FREERTOS_SMP
    spin_lock(&fb_spin_lock);
#endif // CONFIG_FREERTOS_SMP

    return flags;
}

/**
 * @brief Exit critical section
 *
 * @param flags Interrupt flags returned by fb_enter_critical()
 */
static inline void fb_exit_critical(uint32_t flags)
{
#ifdef CONFIG_FREERTOS_SMP
    spin_unlock(&fb_spin_lock);
#endif // CONFIG_FREERTOS_SMP

    rtos_enable_int(flags);
}

/**
 * @brief Get current timestamp (milliseconds)
 */
static uint64_t get_current_timestamp_ms(void)
{
    return rtos_get_time();
}

/**
 * @brief Count the number of set bits in mask (active consumer count)
 */
static uint32_t count_active_consumers(uint32_t mask)
{
    uint32_t count = 0;
    while (mask)
    {
        count += (mask & 1);
        mask >>= 1;
    }
    return count;
}

/**
 * @brief Initialize doubly-linked list
 */
static bk_err_t frame_list_init(frame_list_t *list)
{
    if (list == NULL)
    {
        return BK_FAIL;
    }

    list->head = NULL;
    list->tail = NULL;
    list->count = 0;

    return BK_OK;
}

/**
 * @brief Deinitialize doubly-linked list
 */
static void frame_list_deinit(frame_list_t *list)
{
    if (list == NULL)
    {
        return;
    }

    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

/**
 * @brief Add node to tail of list (lockless version, caller must hold lock)
 */
static void frame_list_add_tail_nolock(frame_list_t *list, frame_node_t *node)
{
    if (list == NULL || node == NULL)
    {
        return;
    }

    node->next = NULL;
    node->prev = list->tail;

    if (list->tail != NULL)
    {
        list->tail->next = node;
    }

    list->tail = node;

    if (list->head == NULL)
    {
        list->head = node;
    }

    list->count++;
}

/**
 * @brief Add node to tail of list (locked version, for task context use)
 */
static void frame_list_add_tail(frame_list_t *list, frame_node_t *node)
{
    if (list == NULL || node == NULL)
    {
        return;
    }

    uint32_t flags = fb_enter_critical();
    frame_list_add_tail_nolock(list, node);
    fb_exit_critical(flags);
}

/**
 * @brief Add node to head of list (lockless version, caller must hold lock)
 */
static void frame_list_add_head_nolock(frame_list_t *list, frame_node_t *node)
{
    if (list == NULL || node == NULL)
    {
        return;
    }

    node->next = list->head;
    node->prev = NULL;

    if (list->head != NULL)
    {
        list->head->prev = node;
    }

    list->head = node;

    if (list->tail == NULL)
    {
        list->tail = node;
    }

    list->count++;
}

/**
 * @brief Add node to head of list (locked version, for task context use)
 */
static void frame_list_add_head(frame_list_t *list, frame_node_t *node)
{
    if (list == NULL || node == NULL)
    {
        return;
    }

    uint32_t flags = fb_enter_critical();
    frame_list_add_head_nolock(list, node);
    fb_exit_critical(flags);
}

/**
 * @brief Remove node from list (lockless version, caller must hold lock)
 */
static void frame_list_remove_nolock(frame_list_t *list, frame_node_t *node)
{
    if (list == NULL || node == NULL)
    {
        return;
    }

    if (node->prev != NULL)
    {
        node->prev->next = node->next;
    }
    else
    {
        list->head = node->next;
    }

    if (node->next != NULL)
    {
        node->next->prev = node->prev;
    }
    else
    {
        list->tail = node->prev;
    }

    list->count--;

    node->next = NULL;
    node->prev = NULL;
}

/**
 * @brief Remove node from list (locked version, for task context use)
 */
static void frame_list_remove(frame_list_t *list, frame_node_t *node)
{
    if (list == NULL || node == NULL)
    {
        return;
    }

    uint32_t flags = fb_enter_critical();
    frame_list_remove_nolock(list, node);
    fb_exit_critical(flags);
}

/**
 * @brief Peek at head node of list (without removing)
 */
static frame_node_t *frame_list_peek_head(frame_list_t *list)
{
    frame_node_t *node = NULL;

    if (list == NULL)
    {
        return NULL;
    }

    uint32_t flags = fb_enter_critical();
    node = list->head;
    fb_exit_critical(flags);

    return node;
}

/**
 * @brief Pop head node from list (remove and return)
 */
static frame_node_t *frame_list_pop_head(frame_list_t *list)
{
    frame_node_t *node = NULL;

    if (list == NULL)
    {
        return NULL;
    }

    uint32_t flags = fb_enter_critical();

    node = list->head;
    if (node != NULL)
    {
        list->head = node->next;
        if (list->head != NULL)
        {
            list->head->prev = NULL;
        }
        else
        {
            list->tail = NULL;
        }
        list->count--;
        node->next = NULL;
        node->prev = NULL;
    }

    fb_exit_critical(flags);

    return node;
}

/**
 * @brief Get frame queue list index
 */
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
 * @brief Create frame node
 */
static frame_node_t *frame_node_create(frame_buffer_t *frame)
{
    frame_node_t *node = (frame_node_t *)os_malloc(sizeof(frame_node_t));
    if (node == NULL)
    {
        return NULL;
    }
    LOG_DEBUG("frame_node_create: node:%p, frame:%p\n", node, frame);
    os_memset(node, 0, sizeof(frame_node_t));
    node->frame = frame;
    node->create_time = get_current_timestamp_ms();

    return node;
}

/**
 * @brief Destroy frame node
 */
static void frame_node_destroy(frame_node_t *node, image_format_t format)
{
    if (node == NULL)
    {
        return;
    }

    if (node->frame != NULL)
    {
        switch (format)
        {
            case IMAGE_MJPEG:
            case IMAGE_H264:
                frame_buffer_encode_free(node->frame);
                break;
            case IMAGE_YUV:
                frame_buffer_display_free(node->frame);
                break;
            default:
                LOGW("%s unknown format:%d\n", __func__, format);
                break;
        }
    }

    os_free(node);
}

/**
 * @brief Initialize frame_queue data structure (V2 version)
 *
 * @param format Image format
 * @return bk_err_t Initialization result
 */
static bk_err_t frame_queue_v2_init(image_format_t format)
{
    bk_err_t ret = BK_FAIL;
    int index = -1;
    uint8_t frame_count = 0;

    // Get index and frame count for corresponding format
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

    // Check if already initialized
    if (frame_queue_v2[index].count > 0)
    {
        LOG_DEBUG("%s, %d already init:%d\n", __func__, __LINE__, format);
        return BK_OK;
    }

    // Initialize frame_queue structure
    os_memset(&frame_queue_v2[index], 0, sizeof(frame_queue_v2_t));
    frame_queue_v2[index].count = frame_count;
    frame_queue_v2[index].format = format;

    // Initialize linked lists
    ret = frame_list_init(&frame_queue_v2[index].free_list);
    if (ret != BK_OK)
    {
        LOGE("%s, %d free_list init fail:%d\n", __func__, __LINE__, ret);
        return ret;
    }

    ret = frame_list_init(&frame_queue_v2[index].ready_list);
    if (ret != BK_OK)
    {
        frame_list_deinit(&frame_queue_v2[index].free_list);
        LOGE("%s, %d ready_list init fail:%d\n", __func__, __LINE__, ret);
        return ret;
    }

    // Initialize frame ready semaphore (max=8, supports up to 8 consumers waiting simultaneously)
    // Initial value=0 indicates no frame ready
    // When complete is called, semaphore will be set multiple times based on active consumer count
    ret = rtos_init_semaphore(&frame_queue_v2[index].frame_ready, 8);
    if (ret != BK_OK)
    {
        frame_list_deinit(&frame_queue_v2[index].ready_list);
        frame_list_deinit(&frame_queue_v2[index].free_list);
        LOGE("%s, %d frame_ready semaphore init fail:%d\n", __func__, __LINE__, ret);
        return ret;
    }

    // Pre-allocate frame nodes (but not actual frame buffers)
    for (int i = 0; i < frame_count; i++)
    {
        frame_node_t *node = frame_node_create(NULL);
        if (node != NULL)
        {
            frame_list_add_tail(&frame_queue_v2[index].free_list, node);
        }
    }

    LOGI("%s format:%d, count:%d\n", __func__, format, frame_count);

    return BK_OK;
}

/**
 * @brief Initialize all frame_queue data structures (V2 version)
 *
 * @return bk_err_t Initialization result
 */
bk_err_t frame_queue_v2_init_all(void)
{
    if (frame_queue_v2_init(IMAGE_MJPEG) != BK_OK)
    {
        return BK_FAIL;
    }

    if (frame_queue_v2_init(IMAGE_H264) != BK_OK)
    {
        return BK_FAIL;
    }

    if (frame_queue_v2_init(IMAGE_YUV) != BK_OK)
    {
        return BK_FAIL;
    }

    return BK_OK;
}

/**
 * @brief Deinitialize frame_queue data structure (V2 version)
 *
 * @param format Image format
 * @return bk_err_t Deinitialization result
 */
static bk_err_t frame_queue_v2_deinit(image_format_t format)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return BK_FAIL;
    }

    if (frame_queue_v2[index].count == 0)
    {
        LOG_DEBUG("%s, %d not init:%d\n", __func__, __LINE__, format);
        return BK_OK;
    }

    // Free all nodes in free_list
    frame_node_t *node;
    while ((node = frame_list_pop_head(&frame_queue_v2[index].free_list)) != NULL)
    {
        frame_node_destroy(node, format);
    }

    // Free all nodes in ready_list
    while ((node = frame_list_pop_head(&frame_queue_v2[index].ready_list)) != NULL)
    {
        frame_node_destroy(node, format);
    }

    frame_list_deinit(&frame_queue_v2[index].free_list);
    frame_list_deinit(&frame_queue_v2[index].ready_list);

    // Deinitialize frame ready semaphore
    rtos_deinit_semaphore(&frame_queue_v2[index].frame_ready);

    // Print statistics
    LOGI("%s format:%d, malloc:%d, complete:%d, free:%d\n",
         __func__, format,
         frame_queue_v2[index].total_malloc,
         frame_queue_v2[index].total_complete,
         frame_queue_v2[index].total_free);

    // Reset frame_queue structure
    os_memset(&frame_queue_v2[index], 0, sizeof(frame_queue_v2_t));

    return BK_OK;
}

/**
 * @brief Deinitialize all frame_queue data structures (V2 version)
 *
 * @return bk_err_t Deinitialization result
 */
bk_err_t frame_queue_v2_deinit_all(void)
{
    frame_queue_v2_deinit(IMAGE_MJPEG);
    frame_queue_v2_deinit(IMAGE_H264);
    frame_queue_v2_deinit(IMAGE_YUV);

    return BK_OK;
}

/**
 * @brief Clear ready queues of all frame_queues (V2 version)
 *
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_v2_clear_all(void)
{
    for (int index = 0; index < FB_LIST_MAX_COUNT; index++)
    {
        if (frame_queue_v2[index].count == 0)
        {
            continue;
        }

        uint32_t cleared_count = 0;
        uint32_t skipped_count = 0;

        // Process all ready_list nodes in one critical section
        uint32_t flags = fb_enter_critical();

        // Iterate ready_list, only clear unused nodes
        frame_node_t *node = frame_queue_v2[index].ready_list.head;
        frame_node_t *next_node = NULL;

        while (node != NULL)
        {
            next_node = node->next;

            // Check if any consumer is accessing
            if (node->ref_count == 0)
            {
                // No consumer holding, safe to clear
                LOG_DEBUG("%s READY_LIST_MOVE: node:%p, frame:%p, ref:0, mask:0x%x, accessed:0x%x\n",
                          __func__, node, node->frame, node->consumer_mask, node->accessed_mask);

                // Remove from ready_list (use nolock version, we're already in critical section)
                frame_list_remove_nolock(&frame_queue_v2[index].ready_list, node);

                // Reset node state (frame buffers will be freed uniformly when free_list is cleaned)
                node->ref_count = 0;
                node->consumer_mask = 0;
                node->accessed_mask = 0;
                node->timestamp = 0;
                node->in_use = 0;  // Clear in-use flag

                // Move to free_list (use nolock version)
                frame_list_add_tail_nolock(&frame_queue_v2[index].free_list, node);
                cleared_count++;
            }
            else
            {
                // Consumer is accessing, skip this node
                // Will be handled automatically when consumer releases
                skipped_count++;
                LOG_DEBUG("%s skip node with ref_count=%d\n", __func__, node->ref_count);
            }

            node = next_node;
        }

        fb_exit_critical(flags);

        // Important: also need to clean nodes in free_list
        // 1. Reset in_use flag (nodes that are malloc'd but not complete'd)
        // 2. Free all frame buffers to save memory
        uint32_t reset_count = 0;
        uint32_t freed_buffer_count = 0;
        flags = fb_enter_critical();

        node = frame_queue_v2[index].free_list.head;
        while (node != NULL)
        {
            // Reset in-use flag
            if (node->in_use == 1)
            {
                node->in_use = 0;
                reset_count++;
            }

            // Free frame buffer to save memory
            if (node->frame != NULL)
            {
                LOG_DEBUG("%s FREE_LIST_CLEANUP: node:%p, frame:%p, in_use:%d, ref:%d\n",
                          __func__, node, node->frame, node->in_use, node->ref_count);

                switch (frame_queue_v2[index].format)
                {
                    case IMAGE_MJPEG:
                    case IMAGE_H264:
                        frame_buffer_encode_free(node->frame);
                        break;
                    case IMAGE_YUV:
                        frame_buffer_display_free(node->frame);
                        break;
                    default:
                        break;
                }
                node->frame = NULL;
                freed_buffer_count++;
            }

            node = node->next;
        }

        fb_exit_critical(flags);

        LOG_DEBUG("%s format:%d, cleared:%d, skipped:%d, reset_in_use:%d, freed_buffers:%d\n",
                  __func__, frame_queue_v2[index].format, cleared_count, skipped_count, reset_count, freed_buffer_count);
    }

    return BK_OK;
}

/**
 * @brief Register consumer
 *
 * @param format Image format
 * @param consumer_id Consumer ID (CONSUMER_XXX)
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_v2_register_consumer(image_format_t format, uint32_t consumer_id)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return BK_FAIL;
    }

    if (frame_queue_v2[index].count == 0)
    {
        LOGE("%s, %d not init:%d\n", __func__, __LINE__, format);
        return BK_FAIL;
    }

    // Find bit index corresponding to consumer_id
    int consumer_idx = -1;
    for (int i = 0; i < 8; i++)
    {
        if (consumer_id & (1 << i))
        {
            consumer_idx = i;
            break;
        }
    }

    if (consumer_idx < 0)
    {
        LOGE("%s invalid consumer_id:0x%x\n", __func__, consumer_id);
        return BK_FAIL;
    }

    // Protect consumer information using global spinlock
    uint32_t flags = fb_enter_critical();

    // Set consumer information
    frame_queue_v2[index].consumers[consumer_idx].consumer_id = consumer_id;
    frame_queue_v2[index].consumers[consumer_idx].enabled = 1;
    frame_queue_v2[index].consumers[consumer_idx].get_count = 0;
    frame_queue_v2[index].consumers[consumer_idx].release_count = 0;

    // Update active consumer mask
    frame_queue_v2[index].active_consumers |= consumer_id;

    // Clean up old frames in ready_list that have consumer_mask = 0
    // These are frames that were completed when no consumers were registered
    // They will never be consumed, so we should move them back to free_list
    frame_node_t *node = frame_queue_v2[index].ready_list.head;
    frame_node_t *next_node = NULL;
    uint32_t cleaned_count = 0;
    uint32_t skipped_count = 0;

    while (node != NULL)
    {
        next_node = node->next;

        if (node->consumer_mask == 0 && node->ref_count == 0)
        {
            // Old frame with no consumers, move back to free_list
            LOG_DEBUG("%s CLEANUP_OLD_FRAME: node:%p, frame:%p, consumer_mask:0x%x\n",
                      __func__, node, node->frame, node->consumer_mask);

            // Remove from ready_list (use nolock version, already in critical section)
            frame_list_remove_nolock(&frame_queue_v2[index].ready_list, node);

            // Reset node state
            node->ref_count = 0;
            node->consumer_mask = 0;
            node->accessed_mask = 0;
            node->timestamp = 0;
            node->in_use = 0;
            // Add to free_list (use nolock version)
            frame_list_add_tail_nolock(&frame_queue_v2[index].free_list, node);
            cleaned_count++;
        }
        else
        {
            skipped_count++;
        }
        node = next_node;
    }

    fb_exit_critical(flags);

    LOGI("%s format:%d, consumer:0x%x, active_consumers:0x%x, cleaned:%d, skipped:%d\n",
         __func__, format, consumer_id, frame_queue_v2[index].active_consumers, cleaned_count, skipped_count);

    return BK_OK;
}

/**
 * @brief Unregister consumer
 *
 * @param format Image format
 * @param consumer_id Consumer ID
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_v2_unregister_consumer(image_format_t format, uint32_t consumer_id)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return BK_FAIL;
    }

    if (frame_queue_v2[index].count == 0)
    {
        LOGE("%s, %d not init:%d\n", __func__, __LINE__, format);
        return BK_FAIL;
    }

    // Find bit index corresponding to consumer_id
    int consumer_idx = -1;
    for (int i = 0; i < 8; i++)
    {
        if (consumer_id & (1 << i))
        {
            consumer_idx = i;
            break;
        }
    }

    if (consumer_idx < 0)
    {
        LOGE("%s invalid consumer_id:0x%x\n", __func__, consumer_id);
        return BK_FAIL;
    }

    // Protect all operations using global spinlock
    uint32_t flags = fb_enter_critical();

    // Update active consumer mask
    frame_queue_v2[index].active_consumers &= ~consumer_id;

    // Clear consumer information
    frame_queue_v2[index].consumers[consumer_idx].enabled = 0;

    // Save statistics for logging
    uint32_t get_count = frame_queue_v2[index].consumers[consumer_idx].get_count;
    uint32_t release_count = frame_queue_v2[index].consumers[consumer_idx].release_count;

    // Iterate ready_list and update consumer_mask of all nodes
    frame_node_t *node = frame_queue_v2[index].ready_list.head;
    frame_node_t *next_node = NULL;
    uint32_t recycled_count = 0;

    while (node != NULL)
    {
        next_node = node->next;

        // If consumer has not accessed yet, remove from consumer_mask
        if ((node->consumer_mask & consumer_id) && !(node->accessed_mask & consumer_id))
        {
            node->consumer_mask &= ~consumer_id;

            // Check if this node should be recycled after removing the consumer
            // Conditions: ref_count = 0 AND (consumer_mask = 0 OR all required consumers have accessed)
            uint32_t all_accessed = (node->accessed_mask & node->consumer_mask) == node->consumer_mask;

            if (node->ref_count == 0 && (node->consumer_mask == 0 || all_accessed))
            {
                LOG_DEBUG("%s RECYCLE_NODE: node:%p, frame:%p, consumer_mask:0x%x, accessed_mask:0x%x (keep buffer)\n",
                          __func__, node, node->frame, node->consumer_mask, node->accessed_mask);

                // Remove from ready_list (use nolock version, already in critical section)
                frame_list_remove_nolock(&frame_queue_v2[index].ready_list, node);

                // Reset node state (keep frame buffer for reuse)
                node->ref_count = 0;
                node->consumer_mask = 0;
                node->accessed_mask = 0;
                node->timestamp = 0;
                node->in_use = 0;

                // Add back to free_list (use nolock version)
                frame_list_add_tail_nolock(&frame_queue_v2[index].free_list, node);
                frame_queue_v2[index].total_free++;
                recycled_count++;
            }
        }

        node = next_node;
    }

    fb_exit_critical(flags);

    LOGI("%s format:%d, consumer:0x%x, get:%d, release:%d, recycled:%d, active_consumers:0x%x\n",
         __func__, format, consumer_id, get_count, release_count, recycled_count,
         frame_queue_v2[index].active_consumers);

    return BK_OK;
}

/**
 * @brief Allocate a frame_buffer from frame_queue (V2 version)
 *
 * @param format Image format
 * @param size Requested frame size
 * @return frame_buffer_t* Allocated frame_buffer, NULL if allocation fails
 */
frame_buffer_t *frame_queue_v2_malloc(image_format_t format, uint32_t size)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return NULL;
    }

    if (frame_queue_v2[index].count == 0)
    {
        LOGE("%s, %d not init:%d\n", __func__, __LINE__, format);
        return NULL;
    }

    frame_node_t *node = NULL;
    frame_buffer_t *frame = NULL;

    // Find an unoccupied node from free_list or reuse from ready_list
    // Use fb_enter_critical (disable interrupt + spinlock), supports interrupt context calling
    // All list operations in ONE critical section for better performance
    uint32_t flags = fb_enter_critical();

    frame_node_t *temp = frame_queue_v2[index].free_list.head;
    uint32_t free_count = 0;
    while (temp != NULL)
    {
        free_count++;
        if (temp->in_use == 0)
        {
            // Found a free node, mark as in-use
            temp->in_use = 1;
            node = temp;
            break;
        }
        temp = temp->next;
    }

    // If free_list is empty, try to get oldest frame from ready_list (MJPEG and YUV only)
    // Important: must check ref_count, only reuse frames that are not in use
    if (node == NULL && (format == IMAGE_MJPEG || format == IMAGE_YUV))
    {
        // Iterate ready_list to find nodes safe for reuse
        // Condition: ref_count=0 (no consumer is holding it)
        // Slow consumers will lose some frames, but system can continue running
        temp = frame_queue_v2[index].ready_list.head;
        while (temp != NULL)
        {
            if (temp->ref_count == 0)
            {
                // Found an unheld node, can be reused
                // Even if some consumers haven't accessed it yet, reuse it (frame dropping strategy)
                node = temp;
                // Operate on list directly in critical section to avoid repeated locking
                frame_list_remove_nolock(&frame_queue_v2[index].ready_list, node);
                // Mark as in-use and add to free_list head
                node->in_use = 1;
                frame_list_add_head_nolock(&frame_queue_v2[index].free_list, node);
                LOG_DEBUG("%s reuse frame from ready_list, ref_count was 0\n", __func__);
                break;
            }

            temp = temp->next;
        }
    }

    fb_exit_critical(flags);

    if (node == NULL)
    {
        LOG_DEBUG("%s no available node! Dumping debug info:\n", __func__);
        //frame_queue_v2_debug_print(format);
        return NULL;
    }

    // Allocate frame buffer if node doesn't have one
    if (node->frame == NULL)
    {
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
                LOGW("%s unknown format:%d\n", __func__, format);
                // Unknown format, reset in_use (node stays in free_list, no need to add)
                node->in_use = 0;
                return NULL;
        }

        if (frame == NULL)
        {
            LOGW("%s malloc frame fail\n", __func__);
            // malloc failed, reset in_use (node stays in free_list, no need to add)
            node->in_use = 0;
            return NULL;
        }

        node->frame = frame;
        LOG_DEBUG("%s MALLOC_NEW: node:%p, frame:%p, size:%d\n", __func__, node, frame, size);
    }
    else
    {
        frame = node->frame;
        LOG_DEBUG("%s MALLOC_REUSE: node:%p, frame:%p (buffer reused)\n", __func__, node, frame);
    }

    // Reset frame information
    frame->type = 0;
    frame->fmt = 0;
    frame->crc = 0;
    frame->timestamp = 0;
    frame->width = 0;
    frame->height = 0;
    frame->length = 0;
    frame->sequence = 0;
    frame->h264_type = 0;

    // Reset node information
    node->ref_count = 0;  // Initially no one holds it (producer not counted in references)
    node->accessed_mask = 0;
    node->timestamp = get_current_timestamp_ms();

    frame_queue_v2[index].total_malloc++;

    return frame;
}

/**
 * @brief Put filled frame into ready queue (V2 version)
 *
 * @param format Image format
 * @param frame frame_buffer to put back into queue
 * @return bk_err_t Operation result
 */
bk_err_t frame_queue_v2_complete(image_format_t format, frame_buffer_t *frame)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return BK_FAIL;
    }

    if (frame_queue_v2[index].count == 0)
    {
        LOGE("%s, %d not init:%d\n", __func__, __LINE__, format);
        return BK_FAIL;
    }

    if (frame == NULL)
    {
        LOGE("%s frame is NULL\n", __func__);
        return BK_FAIL;
    }

    // Find corresponding node from free_list (find nodes with in_use=1 and matching frame)
    // Use fb_enter_critical (disable interrupt + spinlock), supports interrupt context calling
    frame_node_t *node = NULL;
    uint32_t flags = fb_enter_critical();

    frame_node_t *temp = frame_queue_v2[index].free_list.head;
    while (temp != NULL)
    {
        if (temp->in_use == 1 && temp->frame == frame)
        {
            node = temp;
            node->in_use = 0;  // Clear in-use flag
            break;
        }
        temp = temp->next;
    }

    if (node == NULL)
    {
        fb_exit_critical(flags);
        LOGE("%s node not found (in_use or frame mismatch)\n", __func__);
        return BK_FAIL;
    }

    // Remove from free_list (operate directly in critical section to avoid repeated locking)
    frame_list_remove_nolock(&frame_queue_v2[index].free_list, node);

    // Set node attributes
    node->accessed_mask = 0;
    node->timestamp = frame->timestamp > 0 ? frame->timestamp : get_current_timestamp_ms();
    node->create_time = get_current_timestamp_ms();

    // Read active consumer mask
    node->consumer_mask = frame_queue_v2[index].active_consumers;
    uint32_t consumer_count = count_active_consumers(frame_queue_v2[index].active_consumers);

    // Add node to ready_list (operate directly in critical section)
    frame_list_add_tail_nolock(&frame_queue_v2[index].ready_list, node);

    fb_exit_critical(flags);

    LOG_DEBUG("%s COMPLETE: node:%p, frame:%p, mask:0x%x, consumer_count:%d\n",
              __func__, node, node->frame, node->consumer_mask, consumer_count);

    // Wake up waiting consumers (send semaphore multiple times to wake all active consumers)
    // Send at least once; even if no consumers currently, semaphore will accumulate for later consumers
    uint32_t signal_count = consumer_count > 0 ? consumer_count : 1;
    for (uint32_t i = 0; i < signal_count; i++)
    {
        rtos_set_semaphore(&frame_queue_v2[index].frame_ready);
    }

    frame_queue_v2[index].total_complete++;

    return BK_OK;
}

/**
 * @brief Cancel allocated but failed frame (for producer use)
 *
 * @param format Image format
 * @param frame frame_buffer to cancel
 * @return bk_err_t Operation result
 *
 * @note Call this function to release resources when producer encounters error after malloc
 */
bk_err_t frame_queue_v2_cancel(image_format_t format, frame_buffer_t *frame)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return BK_FAIL;
    }

    if (frame_queue_v2[index].count == 0)
    {
        LOGE("%s, %d not init:%d\n", __func__, __LINE__, format);
        return BK_FAIL;
    }

    if (frame == NULL)
    {
        LOGE("%s frame is NULL\n", __func__);
        return BK_FAIL;
    }

    // Find corresponding node from free_list (find nodes with in_use=1 and matching frame)
    // Use fb_enter_critical (disable interrupt + spinlock), supports interrupt context calling
    frame_node_t *node = NULL;
    frame_buffer_t *frame_to_free = NULL;
    uint32_t flags = fb_enter_critical();

    frame_node_t *temp = frame_queue_v2[index].free_list.head;
    while (temp != NULL)
    {
        if (temp->in_use == 1 && temp->frame == frame)
        {
            node = temp;
            // Save frame pointer before clearing, to free outside critical section
            frame_to_free = node->frame;
            node->frame = NULL;
            node->in_use = 0;  // Clear in-use flag last, after all fields are reset
            break;
        }
        temp = temp->next;
    }

    fb_exit_critical(flags);

    if (node == NULL)
    {
        LOGE("%s node not found (in_use or frame mismatch)\n", __func__);
        return BK_FAIL;
    }

    LOG_DEBUG("%s CANCEL: node:%p, frame:%p (release frame buffer)\n", __func__, node, frame_to_free);

    // Free frame buffer outside critical section (may take time)
    if (frame_to_free != NULL)
    {
        switch (format)
        {
            case IMAGE_MJPEG:
            case IMAGE_H264:
                frame_buffer_encode_free(frame_to_free);
                break;
            case IMAGE_YUV:
                frame_buffer_display_free(frame_to_free);
                break;
            default:
                break;
        }

        // Update statistics: increment total_free count
        frame_queue_v2[index].total_free++;
    }

    return BK_OK;
}

/**
 * @brief Consumer get frame (supports multiple consumers accessing same frame, V2 version)
 *
 * @param format Image format
 * @param consumer_id Consumer ID
 * @param timeout Timeout in milliseconds
 * @return frame_buffer_t* Retrieved frame_buffer, NULL if retrieval fails
 */
frame_buffer_t *frame_queue_v2_get_frame(image_format_t format, uint32_t consumer_id, uint32_t timeout)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return NULL;
    }

    if (frame_queue_v2[index].count == 0)
    {
        LOGE("%s, %d not init:%d\n", __func__, __LINE__, format);
        return NULL;
    }

    // Find consumer index
    int consumer_idx = -1;
    for (int i = 0; i < 8; i++)
    {
        if (consumer_id & (1 << i))
        {
            consumer_idx = i;
            break;
        }
    }

    // Check if consumer is enabled (use global spinlock)
    uint32_t flags = fb_enter_critical();
    uint8_t enabled = (consumer_idx >= 0) && frame_queue_v2[index].consumers[consumer_idx].enabled;
    fb_exit_critical(flags);

    if (consumer_idx < 0 || !enabled)
    {
        LOGE("%s invalid or disabled consumer:0x%x\n", __func__, consumer_id);
        return NULL;
    }

    uint64_t start_time = get_current_timestamp_ms();
    frame_buffer_t *found_frame = NULL;

    // Try to get frame, with retry logic
    while (1)
    {
        // Step 1: Search ready_list for unaccessed frame
        flags = fb_enter_critical();

        frame_node_t *node = frame_queue_v2[index].ready_list.head;
        while (node != NULL)
        {
            // Check if this frame needs to be accessed by this consumer and hasn't been accessed yet
            LOG_DEBUG("%s format:%d, consumer:0x%x, consumer_mask:0x%x, accessed_mask:0x%x\n",
                      __func__, format, consumer_id, node->consumer_mask, node->accessed_mask);

            if ((node->consumer_mask & consumer_id) && !(node->accessed_mask & consumer_id))
            {
                // Found available frame
                node->ref_count++;
                node->accessed_mask |= consumer_id;
                found_frame = node->frame;

                // Update consumer statistics (in same critical section)
                frame_queue_v2[index].consumers[consumer_idx].get_count++;

                LOG_DEBUG("%s GET: node:%p, frame:%p, consumer:0x%x, ref_count:%d\n",
                          __func__, node, found_frame, consumer_id, node->ref_count);

                fb_exit_critical(flags);
                return found_frame;
            }

            node = node->next;
        }

        fb_exit_critical(flags);

        // Step 2: Not found, calculate remaining timeout
        uint32_t wait_timeout;
        if (timeout == BEKEN_WAIT_FOREVER)
        {
            wait_timeout = BEKEN_WAIT_FOREVER;
        }
        else
        {
            uint64_t elapsed = get_current_timestamp_ms() - start_time;
            if (elapsed >= timeout)
            {
                // Timeout, return NULL
                LOG_DEBUG("%s timeout, no available frame\n", __func__);
                return NULL;
            }
            wait_timeout = timeout - elapsed;
        }

        // Step 3: Wait for new frame signal
        // complete will send semaphore multiple times, ensuring all consumers can wake up
        bk_err_t ret = rtos_get_semaphore(&frame_queue_v2[index].frame_ready, wait_timeout);
        if (ret != BK_OK)
        {
            // Timeout, no new frame arrived
            LOG_DEBUG("%s semaphore timeout\n", __func__);
            return NULL;
        }

        // Step 4: Woken up, loop back to search again
        LOG_DEBUG("%s woken up, retry search\n", __func__);
    }

    // Should never reach here
    return NULL;
}

/**
 * @brief Consumer release frame (V2 version)
 *
 * @param format Image format
 * @param consumer_id Consumer ID
 * @param frame frame_buffer to release
 */
void frame_queue_v2_release_frame(image_format_t format, uint32_t consumer_id, frame_buffer_t *frame)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        LOGE("%s, %d index:%d\n", __func__, __LINE__, index);
        return;
    }

    if (frame_queue_v2[index].count == 0)
    {
        LOGE("%s, %d not init:%d\n", __func__, __LINE__, format);
        return;
    }

    if (frame == NULL)
    {
        LOGE("%s frame is NULL\n", __func__);
        return;
    }

    // Find consumer index
    int consumer_idx = -1;
    for (int i = 0; i < 8; i++)
    {
        if (consumer_id & (1 << i))
        {
            consumer_idx = i;
            break;
        }
    }

    if (consumer_idx < 0)
    {
        LOGE("%s invalid consumer:0x%x\n", __func__, consumer_id);
        return;
    }

    uint32_t flags = fb_enter_critical();

    // Find corresponding node
    frame_node_t *node = frame_queue_v2[index].ready_list.head;
    frame_node_t *found_node = NULL;
    uint32_t should_final_release = 0;

    while (node != NULL)
    {
        if (node->frame == frame)
        {
            // Decrease reference count
#if FRAME_QUEUE_V2_DEBUG_LOG
            uint32_t old_ref = node->ref_count;
#endif
            if (node->ref_count > 0)
            {
                node->ref_count--;
            }

            // Update consumer statistics (in same critical section)
            frame_queue_v2[index].consumers[consumer_idx].release_count++;

            LOG_DEBUG("%s RELEASE: node:%p, frame:%p, consumer:0x%x, ref_count:%d->%d, mask:0x%x, accessed:0x%x\n",
                      __func__, node, frame, consumer_id, old_ref, node->ref_count,
                      node->consumer_mask, node->accessed_mask);

            // Check if all consumers that need to access have accessed
            uint32_t all_accessed = (node->accessed_mask & node->consumer_mask) == node->consumer_mask;

            // If reference count is 0 and all consumers have accessed, need to release
            if (node->ref_count == 0 && all_accessed)
            {
                should_final_release = 1;
                found_node = node;

                // Remove from ready_list in critical section (use nolock version)
                frame_list_remove_nolock(&frame_queue_v2[index].ready_list, node);
            }

            break;
        }

        node = node->next;
    }

    fb_exit_critical(flags);

    // Perform final release outside critical section to minimize lock holding time
    if (should_final_release && found_node != NULL)
    {
        // Free frame buffer to prevent memory leak and double free
        if (found_node->frame != NULL)
        {
            LOG_DEBUG("%s FINAL_RELEASE: node:%p, frame:%p, consumer:0x%x, ref_count:0, all_accessed:1\n",
                      __func__, found_node, found_node->frame, consumer_id);

            switch (format)
            {
                case IMAGE_MJPEG:
                case IMAGE_H264:
                    frame_buffer_encode_free(found_node->frame);
                    break;
                case IMAGE_YUV:
                    frame_buffer_display_free(found_node->frame);
                    break;
                default:
                    break;
            }
            found_node->frame = NULL;  // Prevent double free
        }
        else
        {
            LOG_DEBUG("%s FINAL_RELEASE but frame is NULL! node:%p, consumer:0x%x\n",
                      __func__, found_node, consumer_id);
        }

        // Reset node and return to free_list
        found_node->ref_count = 0;
        found_node->consumer_mask = 0;
        found_node->accessed_mask = 0;
        found_node->timestamp = 0;
        found_node->in_use = 0;  // Clear in-use flag

        frame_list_add_tail(&frame_queue_v2[index].free_list, found_node);
        frame_queue_v2[index].total_free++;

        return;
    }

    if (found_node == NULL)
    {
        LOG_DEBUG("%s FRAME_NOT_FOUND: frame:%p, consumer:0x%x (already released or invalid)\n",
                  __func__, frame, consumer_id);
    }
}

/**
 * @brief Debug function: print detailed queue status
 */
static void frame_queue_v2_debug_print(image_format_t format)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT || frame_queue_v2[index].count == 0)
    {
        return;
    }

    LOG_DEBUG("======= Format:%d Debug Info =======\n", format);
    LOG_DEBUG("Total nodes: %d\n", frame_queue_v2[index].count);
    LOG_DEBUG("Stats: malloc:%d, complete:%d, free:%d\n",
              frame_queue_v2[index].total_malloc,
              frame_queue_v2[index].total_complete,
              frame_queue_v2[index].total_free);

    // Print free_list
    LOG_DEBUG("--- Free List (count:%d) ---\n", frame_queue_v2[index].free_list.count);
    uint32_t flags = fb_enter_critical();
    frame_node_t *node = frame_queue_v2[index].free_list.head;
    while (node != NULL)
    {
        LOG_DEBUG("  node:%p, frame:%p, in_use:%d, ref:%d\n",
                  node, node->frame, node->in_use, node->ref_count);
        node = node->next;
    }
    fb_exit_critical(flags);

    // Print ready_list
    LOG_DEBUG("--- Ready List (count:%d) ---\n", frame_queue_v2[index].ready_list.count);
    flags = fb_enter_critical();
    node = frame_queue_v2[index].ready_list.head;
    while (node != NULL)
    {
        LOG_DEBUG("  node:%p, frame:%p, ref:%d, mask:0x%x, accessed:0x%x\n",
                  node, node->frame, node->ref_count, node->consumer_mask, node->accessed_mask);
        node = node->next;
    }
    fb_exit_critical(flags);
    LOG_DEBUG("=====================================\n");
}

/**
 * @brief Get queue statistics (V2 version)
 */
bk_err_t frame_queue_v2_get_stats(image_format_t format,
                                  uint32_t *free_count,
                                  uint32_t *ready_count,
                                  uint32_t *total_malloc,
                                  uint32_t *total_complete,
                                  uint32_t *total_free)
{
    int index = get_frame_buffer_list_index(format);
    if (index < 0 || index >= FB_LIST_MAX_COUNT)
    {
        return BK_FAIL;
    }

    if (frame_queue_v2[index].count == 0)
    {
        return BK_FAIL;
    }

    if (free_count)
    {
        *free_count = frame_queue_v2[index].free_list.count;
    }
    if (ready_count)
    {
        *ready_count = frame_queue_v2[index].ready_list.count;
    }
    if (total_malloc)
    {
        *total_malloc = frame_queue_v2[index].total_malloc;
    }
    if (total_complete)
    {
        *total_complete = frame_queue_v2[index].total_complete;
    }
    if (total_free)
    {
        *total_free = frame_queue_v2[index].total_free;
    }

    // Optional: enable detailed debug
    frame_queue_v2_debug_print(format);

    return BK_OK;
}

