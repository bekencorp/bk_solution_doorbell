#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
#include <common/avdk_pixel_types.h>
#endif

#include "doorbell_downlink_img_manager.h"
#include "doorbell_downlink_video.h"

#define TAG "db-dl-mgr"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define DL_MGR_MAX_SLOTS 8U

typedef struct
{
    downlink_frame_t slots[DL_MGR_MAX_SLOTS];
#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
    /* Persistent frame_buffer view of each slot for the zero-copy unfragment
     * path: fb[i].frame aliases slots[i].data. Index-aligned with slots[]. */
    frame_buffer_t fb[DL_MGR_MAX_SLOTS];
#endif

    /* free stack: LIFO of available slot indexes, mutex-protected. */
    uint8_t free_idx[DL_MGR_MAX_SLOTS];
    uint32_t free_top;

    /* ready FIFO: slot indexes waiting for the consumer. */
    uint8_t ready_idx[DL_MGR_MAX_SLOTS];
    uint32_t ready_head;
    uint32_t ready_tail;
    uint32_t ready_count;

    uint32_t slot_count;
    beken_mutex_t lock;
    beken_semaphore_t ready_sem;
    volatile uint8_t inited;
} dl_mgr_t;

static dl_mgr_t s_mgr;

static int dl_slot_index(const downlink_frame_t *frame)
{
    uint32_t i;

    for (i = 0; i < s_mgr.slot_count; i++)
    {
        if (&s_mgr.slots[i] == frame)
        {
            return (int)i;
        }
    }
    return -1;
}

bk_err_t doorbell_downlink_img_manager_init(uint32_t slot_count, uint32_t slot_capacity)
{
    uint32_t i;
    bk_err_t ret;

    if (s_mgr.inited)
    {
        return BK_OK;
    }
    if (slot_count == 0 || slot_count > DL_MGR_MAX_SLOTS || slot_capacity == 0)
    {
        return BK_ERR_PARAM;
    }

    os_memset(&s_mgr, 0, sizeof(s_mgr));
    s_mgr.slot_count = slot_count;

    ret = rtos_init_mutex(&s_mgr.lock);
    if (ret != BK_OK)
    {
        return ret;
    }
    ret = rtos_init_semaphore(&s_mgr.ready_sem, (int)slot_count);
    if (ret != BK_OK)
    {
        rtos_deinit_mutex(&s_mgr.lock);
        return ret;
    }

    for (i = 0; i < slot_count; i++)
    {
        s_mgr.slots[i].data = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, slot_capacity);
        if (s_mgr.slots[i].data == NULL)
        {
            LOGE("alloc slot %u (cap=%u) failed\n", (unsigned)i, (unsigned)slot_capacity);
            goto err;
        }
        s_mgr.slots[i].capacity = slot_capacity;
        s_mgr.slots[i].size = 0;
        s_mgr.free_idx[i] = (uint8_t)i;
#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
        /* Bind the persistent frame_buffer view onto this slot's coded buffer. */
        s_mgr.fb[i].frame = s_mgr.slots[i].data;
        s_mgr.fb[i].size = slot_capacity;
        s_mgr.fb[i].length = 0;
#endif
    }
    s_mgr.free_top = slot_count;
    s_mgr.ready_head = 0;
    s_mgr.ready_tail = 0;
    s_mgr.ready_count = 0;
    s_mgr.inited = 1;

    LOGI("downlink mgr ready: %u slots x %u bytes\n", (unsigned)slot_count, (unsigned)slot_capacity);
    return BK_OK;

err:
    for (i = 0; i < slot_count; i++)
    {
        if (s_mgr.slots[i].data != NULL)
        {
            bk_frame_buffer_free(s_mgr.slots[i].data);
            s_mgr.slots[i].data = NULL;
        }
    }
    rtos_deinit_semaphore(&s_mgr.ready_sem);
    rtos_deinit_mutex(&s_mgr.lock);
    return BK_ERR_NO_MEM;
}

void doorbell_downlink_img_manager_deinit(void)
{
    uint32_t i;

    if (!s_mgr.inited)
    {
        return;
    }
    s_mgr.inited = 0;

    for (i = 0; i < s_mgr.slot_count; i++)
    {
        if (s_mgr.slots[i].data != NULL)
        {
            bk_frame_buffer_free(s_mgr.slots[i].data);
            s_mgr.slots[i].data = NULL;
        }
    }
    rtos_deinit_semaphore(&s_mgr.ready_sem);
    rtos_deinit_mutex(&s_mgr.lock);
    os_memset(&s_mgr, 0, sizeof(s_mgr));
}

downlink_frame_t *doorbell_downlink_free_request(void)
{
    downlink_frame_t *frame = NULL;

    if (!s_mgr.inited)
    {
        return NULL;
    }

    rtos_lock_mutex(&s_mgr.lock);
    if (s_mgr.free_top > 0)
    {
        uint8_t idx = s_mgr.free_idx[--s_mgr.free_top];
        frame = &s_mgr.slots[idx];
        frame->size = 0;
    }
    rtos_unlock_mutex(&s_mgr.lock);

    return frame;
}

bk_err_t doorbell_downlink_ready_push(downlink_frame_t *frame)
{
    int idx;

    if (!s_mgr.inited || frame == NULL)
    {
        return BK_ERR_PARAM;
    }

    rtos_lock_mutex(&s_mgr.lock);
    idx = dl_slot_index(frame);
    if (idx < 0 || s_mgr.ready_count >= s_mgr.slot_count)
    {
        rtos_unlock_mutex(&s_mgr.lock);
        return BK_FAIL;
    }
    s_mgr.ready_idx[s_mgr.ready_tail] = (uint8_t)idx;
    s_mgr.ready_tail = (s_mgr.ready_tail + 1) % s_mgr.slot_count;
    s_mgr.ready_count++;
    rtos_unlock_mutex(&s_mgr.lock);

    /* Single chokepoint for a complete WiFi-received access unit (both the copy
     * and zero-copy producers publish here): count it for the downlink stats. */
    doorbell_downlink_video_stats_on_recv(frame->size);

    rtos_set_semaphore(&s_mgr.ready_sem);
    return BK_OK;
}

downlink_frame_t *doorbell_downlink_ready_pop(uint32_t timeout_ms)
{
    downlink_frame_t *frame = NULL;

    if (!s_mgr.inited)
    {
        return NULL;
    }

    if (rtos_get_semaphore(&s_mgr.ready_sem, timeout_ms) != BK_OK)
    {
        return NULL;
    }
    if (!s_mgr.inited)
    {
        return NULL;
    }

    rtos_lock_mutex(&s_mgr.lock);
    if (s_mgr.ready_count > 0)
    {
        uint8_t idx = s_mgr.ready_idx[s_mgr.ready_head];
        s_mgr.ready_head = (s_mgr.ready_head + 1) % s_mgr.slot_count;
        s_mgr.ready_count--;
        frame = &s_mgr.slots[idx];
    }
    rtos_unlock_mutex(&s_mgr.lock);

    return frame;
}

bk_err_t doorbell_downlink_free_push(downlink_frame_t *frame)
{
    int idx;

    if (!s_mgr.inited || frame == NULL)
    {
        return BK_ERR_PARAM;
    }

    rtos_lock_mutex(&s_mgr.lock);
    idx = dl_slot_index(frame);
    if (idx < 0 || s_mgr.free_top >= s_mgr.slot_count)
    {
        rtos_unlock_mutex(&s_mgr.lock);
        return BK_FAIL;
    }
    frame->size = 0;
    s_mgr.free_idx[s_mgr.free_top++] = (uint8_t)idx;
    rtos_unlock_mutex(&s_mgr.lock);

    return BK_OK;
}

#if CONFIG_SMART_INTERCOM_DL_ZEROCOPY
frame_buffer_t *doorbell_downlink_slot_fb_alloc(uint32_t size)
{
    downlink_frame_t *slot;
    int idx;

    /* The transport passes its JPEG_FRAME_SIZE hint here, but we own the
     * buffer: slots are sized by dl_slot_capacity_for_resolution(), not the
     * hint. The reassembly layer still guards overflow against capacity. */
    (void)size;

    if (!s_mgr.inited)
    {
        return NULL;
    }

    slot = doorbell_downlink_free_request();
    if (slot == NULL)
    {
        doorbell_downlink_video_notify_ref_break();
        return NULL;
    }

    idx = dl_slot_index(slot);
    if (idx < 0)
    {
        (void)doorbell_downlink_free_push(slot);
        return NULL;
    }

    s_mgr.fb[idx].frame = slot->data;
    s_mgr.fb[idx].size = slot->capacity;
    s_mgr.fb[idx].length = 0;
    return &s_mgr.fb[idx];
}

bk_err_t doorbell_downlink_slot_fb_commit(frame_buffer_t *fb)
{
    uint32_t idx;

    if (!s_mgr.inited || fb == NULL)
    {
        return BK_ERR_PARAM;
    }
    if (fb < &s_mgr.fb[0] || fb >= &s_mgr.fb[s_mgr.slot_count])
    {
        return BK_ERR_PARAM;
    }

    idx = (uint32_t)(fb - &s_mgr.fb[0]);
    s_mgr.slots[idx].size = fb->length;
    return doorbell_downlink_ready_push(&s_mgr.slots[idx]);
}

bk_err_t doorbell_downlink_slot_fb_release(frame_buffer_t *fb)
{
    uint32_t idx;

    if (fb == NULL)
    {
        return BK_ERR_PARAM;
    }
    /* Tolerate release after deinit: range-check against the whole array (the
     * pool may have been torn down while the transfer layer still held a fb). */
    if (fb < &s_mgr.fb[0] || fb >= &s_mgr.fb[DL_MGR_MAX_SLOTS])
    {
        return BK_ERR_PARAM;
    }

    idx = (uint32_t)(fb - &s_mgr.fb[0]);
    return doorbell_downlink_free_push(&s_mgr.slots[idx]);
}
#endif /* CONFIG_SMART_INTERCOM_DL_ZEROCOPY */
