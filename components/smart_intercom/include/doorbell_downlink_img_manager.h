#ifndef __DOORBELL_DOWNLINK_IMG_MANAGER_H__
#define __DOORBELL_DOWNLINK_IMG_MANAGER_H__

#include <stdint.h>
#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One downlink H.264 access-unit buffer slot.
 *
 * Mirrors the uplink encoded-data manager (bk_encoded_data_manager) but for the
 * receive direction: the network producer fills @c data / @c size, the decode
 * consumer reads it. @c capacity is the fixed allocation size of @c data.
 */
typedef struct
{
    uint8_t *data;      /**< coded-heap buffer, @c capacity bytes            */
    uint32_t size;      /**< valid access-unit bytes filled by the producer  */
    uint32_t capacity;  /**< allocation size of @c data                      */
} downlink_frame_t;

/**
 * @brief Initialize the downlink frame manager.
 *
 * Allocates @p slot_count coded-heap buffers of @p slot_capacity bytes each and
 * arranges them in a free/ready queue pair.
 *
 * @param slot_count     Number of buffer slots (producer/consumer depth).
 * @param slot_capacity  Byte capacity of each slot (max access-unit size).
 * @return BK_OK on success.
 */
bk_err_t doorbell_downlink_img_manager_init(uint32_t slot_count, uint32_t slot_capacity);

/** @brief Release all slots and queues. Safe to call when not initialized. */
void doorbell_downlink_img_manager_deinit(void);

/**
 * @brief Producer: acquire a free slot (non-blocking).
 * @return Slot pointer, or NULL if no free slot is available.
 */
downlink_frame_t *doorbell_downlink_free_request(void);

/** @brief Producer: publish a filled slot to the ready queue. */
bk_err_t doorbell_downlink_ready_push(downlink_frame_t *frame);

/**
 * @brief Consumer: pop the next ready slot.
 * @param timeout_ms  Max wait; use BEKEN_WAIT_FOREVER to block.
 * @return Slot pointer, or NULL on timeout / shutdown.
 */
downlink_frame_t *doorbell_downlink_ready_pop(uint32_t timeout_ms);

/** @brief Consumer: return a slot to the free queue. */
bk_err_t doorbell_downlink_free_push(downlink_frame_t *frame);

#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
#include <common/avdk_pixel_types.h>

/*
 * Zero-copy view of the slot pool as frame_buffer_t descriptors, so the
 * bk_network_transfer unfragment layer can reassemble a downlink access unit
 * DIRECTLY into a slot (see 下行视频零拷贝优化方案.md). Each slot owns one
 * persistent frame_buffer_t whose ->frame points at the slot's coded buffer.
 */

/**
 * @brief Producer (unfragment malloc_cb): acquire a free slot as a frame_buffer.
 *
 * @param size Requested capacity; must be <= slot capacity or NULL is returned.
 * @return frame_buffer_t* whose @c frame points at the slot buffer and @c size
 *         is the slot capacity, or NULL if not initialized / no free slot / too
 *         big. On NULL the transfer layer drops the frame (clean per-frame drop).
 */
frame_buffer_t *doorbell_downlink_slot_fb_alloc(uint32_t size);

/**
 * @brief Producer (unfragment send_cb): publish a reassembled slot to ready.
 *
 * The access unit is already in the slot (reassembled in place); this only
 * records @c fb->length as the valid size and pushes to the ready FIFO. No copy.
 */
bk_err_t doorbell_downlink_slot_fb_commit(frame_buffer_t *fb);

/**
 * @brief unfragment free_cb: return a slot's frame_buffer to the free stack.
 */
bk_err_t doorbell_downlink_slot_fb_release(frame_buffer_t *fb);
#endif /* CONFIG_SMART_INTERCOM_DL_ZEROCOPY */

#ifdef __cplusplus
}
#endif

#endif /* __DOORBELL_DOWNLINK_IMG_MANAGER_H__ */
