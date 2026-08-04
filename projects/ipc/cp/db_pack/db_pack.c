#include "db_pack.h"
#include <os/mem.h>
#include <common/bk_crc.h>
#include <driver/aon_rtc.h>

#define DB_PACK_TAG "DB_PACK"

#define LOGI(...)   BK_LOGI(DB_PACK_TAG, ##__VA_ARGS__)
#define LOGW(...)   BK_LOGW(DB_PACK_TAG, ##__VA_ARGS__)
#define LOGE(...)   BK_LOGE(DB_PACK_TAG, ##__VA_ARGS__)
#define LOGD(...)   BK_LOGD(DB_PACK_TAG, ##__VA_ARGS__)
#define LOGV(...)   BK_LOGV(DB_PACK_TAG, ##__VA_ARGS__)

static db_pack_chan_t *s_db_pack_chan = NULL;

/**
 * @brief Get current time in milliseconds
 * @return uint32_t Current time in milliseconds
 */
static uint32_t db_pack_get_milliseconds(void)
{
    uint64_t ms = bk_aon_rtc_get_ms();
    return (uint32_t)(ms & 0xFFFFFFFF);
}

bk_err_t db_pack_init(uint16_t max_rx_size, uint16_t max_tx_size)
{
    if (s_db_pack_chan != NULL) {
        LOGW("%s: Already initialized\n", __func__);
        return BK_OK;
    }

    s_db_pack_chan = (db_pack_chan_t *)os_malloc(sizeof(db_pack_chan_t));
    if (s_db_pack_chan == NULL) {
        LOGE("%s: Failed to allocate channel\n", __func__);
        return BK_ERR_NO_MEM;
    }

    os_memset(s_db_pack_chan, 0, sizeof(db_pack_chan_t));

    // Allocate cache buffer for receive (with header)
    s_db_pack_chan->cbuf = os_malloc(max_rx_size + DB_PACK_HEAD_SIZE_TOTAL);
    if (s_db_pack_chan->cbuf == NULL) {
        LOGE("%s: Failed to allocate cache buffer\n", __func__);
        os_free(s_db_pack_chan);
        s_db_pack_chan = NULL;
        return BK_ERR_NO_MEM;
    }

    s_db_pack_chan->csize = max_rx_size + DB_PACK_HEAD_SIZE_TOTAL;

    // Allocate transmit buffer (with header)
    s_db_pack_chan->tbuf = os_malloc(max_tx_size + DB_PACK_HEAD_SIZE_TOTAL);
    if (s_db_pack_chan->tbuf == NULL) {
        LOGE("%s: Failed to allocate transmit buffer\n", __func__);
        os_free(s_db_pack_chan->cbuf);
        os_free(s_db_pack_chan);
        s_db_pack_chan = NULL;
        return BK_ERR_NO_MEM;
    }

    s_db_pack_chan->tsize = max_tx_size;
    s_db_pack_chan->sequence = 0;

    LOGD("%s: Initialized, max_rx=%u, max_tx=%u\n", __func__, max_rx_size, max_tx_size);
    return BK_OK;
}

bk_err_t db_pack_deinit(void)
{
    if (s_db_pack_chan == NULL) {
        LOGW("%s: Not initialized\n", __func__);
        return BK_OK;
    }

    if (s_db_pack_chan->cbuf) {
        os_free(s_db_pack_chan->cbuf);
        s_db_pack_chan->cbuf = NULL;
    }

    if (s_db_pack_chan->tbuf) {
        os_free(s_db_pack_chan->tbuf);
        s_db_pack_chan->tbuf = NULL;
    }

    os_free(s_db_pack_chan);
    s_db_pack_chan = NULL;

    LOGD("%s: Deinitialized\n", __func__);
    return BK_OK;
}

int db_pack_pack(uint8_t *data, uint32_t length, uint8_t **pack_ptr, uint32_t *pack_ptr_length)
{
    if (s_db_pack_chan == NULL) {
        LOGE("%s: Not initialized\n", __func__);
        return -1;
    }

    if (data == NULL || pack_ptr == NULL || pack_ptr_length == NULL) {
        LOGE("%s: Invalid parameters\n", __func__);
        return -1;
    }

    if (length > s_db_pack_chan->tsize) {
        LOGE("%s: Data length %u exceeds max tx size %u\n", __func__, length, s_db_pack_chan->tsize);
        return -1;
    }

    db_pack_head_t *head = (db_pack_head_t *)s_db_pack_chan->tbuf;

    /*
     *   Magic code  2 bytes
     *   Flags       2 bytes
     *   Timestamp   4 bytes
     *   Sequence    2 bytes
     *   Length      2 bytes
     *   CRC         1 byte
     *   RESERVED    3 bytes
     */
    head->magic = DB_PACK_CHECK_ENDIAN_UINT16(DB_PACK_HEAD_MAGIC_CODE);
    head->flags = DB_PACK_CHECK_ENDIAN_UINT16(DB_PACK_HEAD_FLAGS_CRC);
    head->timestamp = DB_PACK_CHECK_ENDIAN_UINT32(db_pack_get_milliseconds());
    head->sequence = DB_PACK_CHECK_ENDIAN_UINT16(++s_db_pack_chan->sequence);
    head->length = DB_PACK_CHECK_ENDIAN_UINT16(length);
    head->crc = hnd_crc8(data, length, DB_PACK_CRC8_INIT_VALUE);
    head->reserved[0] = 0;
    head->reserved[1] = 0;
    head->reserved[2] = 0;

    os_memcpy(head->payload, data, length);

    *pack_ptr = s_db_pack_chan->tbuf;
    *pack_ptr_length = DB_PACK_HEAD_SIZE_TOTAL + length;

    return (int)*pack_ptr_length;
}

int db_pack_unpack(uint8_t *data, uint32_t length, db_pack_recv_cb_t recv_cb)
{
    if (s_db_pack_chan == NULL) {
        LOGE("%s: Not initialized\n", __func__);
        return -1;
    }

    if (data == NULL || length == 0) {
        LOGE("%s: Invalid parameters\n", __func__);
        return -1;
    }

    db_pack_head_t head, *ptr;
    uint8_t *p = data;
    uint32_t left = length;
    int cp_len = 0;

    LOGV("%s: Unpack data, length=%u\n", __func__, length);

    while (left != 0) {
        if (s_db_pack_chan->ccount == 0) {
            // No cached data, try to parse header from incoming data
            if (left < DB_PACK_HEAD_SIZE_TOTAL) {
                // Not enough data for header, cache it
                os_memcpy(s_db_pack_chan->cbuf + s_db_pack_chan->ccount, p, left);
                s_db_pack_chan->ccount += left;
                break;
            }

            ptr = (db_pack_head_t *)p;

            head.magic = DB_PACK_CHECK_ENDIAN_UINT16(ptr->magic);

            if (head.magic == DB_PACK_HEAD_MAGIC_CODE) {
                head.flags = DB_PACK_CHECK_ENDIAN_UINT16(ptr->flags);
                head.timestamp = DB_PACK_CHECK_ENDIAN_UINT32(ptr->timestamp);
                head.sequence = DB_PACK_CHECK_ENDIAN_UINT16(ptr->sequence);
                head.length = DB_PACK_CHECK_ENDIAN_UINT16(ptr->length);
                head.crc = ptr->crc;
                head.reserved[0] = ptr->reserved[0];
                head.reserved[1] = ptr->reserved[1];
                head.reserved[2] = ptr->reserved[2];
            } else {
                LOGE("%s: Invalid magic code: 0x%04X\n", __func__, head.magic);
                break;
            }

            if (left < head.length + DB_PACK_HEAD_SIZE_TOTAL) {
                // Not enough data for complete packet, cache it
                os_memcpy(s_db_pack_chan->cbuf + s_db_pack_chan->ccount, p, left);
                s_db_pack_chan->ccount += left;
                break;
            }

            // Verify CRC
            if (DB_PACK_HEAD_FLAGS_CRC & head.flags) {
                uint8_t calc_crc = hnd_crc8(p + DB_PACK_HEAD_SIZE_TOTAL, head.length, DB_PACK_CRC8_INIT_VALUE);
                if (calc_crc != head.crc) {
                    LOGW("%s: CRC mismatch, expected=0x%02X, calculated=0x%02X\n", 
                         __func__, head.crc, calc_crc);
                }
            }

            // Complete packet received, call callback
            if (recv_cb) {
                recv_cb(ptr->payload, head.length);
            }

            p += DB_PACK_HEAD_SIZE_TOTAL + head.length;
            left -= DB_PACK_HEAD_SIZE_TOTAL + head.length;
        } else {
            // Have cached data, try to complete the packet
            if (s_db_pack_chan->ccount < DB_PACK_HEAD_SIZE_TOTAL) {
                // Need more data for header
                cp_len = DB_PACK_HEAD_SIZE_TOTAL - s_db_pack_chan->ccount;

                if (cp_len < 0) {
                    LOGE("%s: Invalid cp_len: %d\n", __func__, cp_len);
                    break;
                }

                if (left < cp_len) {
                    // Not enough data, cache what we have
                    os_memcpy(s_db_pack_chan->cbuf + s_db_pack_chan->ccount, p, left);
                    s_db_pack_chan->ccount += left;
                    left = 0;
                    break;
                } else {
                    // Complete header, copy it
                    os_memcpy(s_db_pack_chan->cbuf + s_db_pack_chan->ccount, p, cp_len);
                    s_db_pack_chan->ccount += cp_len;
                    p += cp_len;
                    left -= cp_len;
                }
            }

            ptr = (db_pack_head_t *)s_db_pack_chan->cbuf;

            head.magic = DB_PACK_CHECK_ENDIAN_UINT16(ptr->magic);

            if (head.magic == DB_PACK_HEAD_MAGIC_CODE) {
                head.flags = DB_PACK_CHECK_ENDIAN_UINT16(ptr->flags);
                head.timestamp = DB_PACK_CHECK_ENDIAN_UINT32(ptr->timestamp);
                head.sequence = DB_PACK_CHECK_ENDIAN_UINT16(ptr->sequence);
                head.length = DB_PACK_CHECK_ENDIAN_UINT16(ptr->length);
                head.crc = ptr->crc;
                head.reserved[0] = ptr->reserved[0];
                head.reserved[1] = ptr->reserved[1];
                head.reserved[2] = ptr->reserved[2];
            } else {
                LOGE("%s: Invalid cached magic code: 0x%04X\n", __func__, head.magic);
                s_db_pack_chan->ccount = 0; // Reset cache
                break;
            }

            if (s_db_pack_chan->ccount < DB_PACK_HEAD_SIZE_TOTAL + head.length) {
                // Need more data for payload
                cp_len = head.length + DB_PACK_HEAD_SIZE_TOTAL - s_db_pack_chan->ccount;

                if (cp_len < 0) {
                    LOGE("%s: Invalid cp_len: %d\n", __func__, cp_len);
                    s_db_pack_chan->ccount = 0;
                    break;
                }

                if (left < cp_len) {
                    // Not enough data, cache what we have
                    os_memcpy(s_db_pack_chan->cbuf + s_db_pack_chan->ccount, p, left);
                    s_db_pack_chan->ccount += left;
                    left = 0;
                    break;
                } else {
                    // Complete packet, copy remaining payload
                    os_memcpy(s_db_pack_chan->cbuf + s_db_pack_chan->ccount, p, cp_len);
                    left -= cp_len;
                    p += cp_len;
                    s_db_pack_chan->ccount += cp_len;
                }
            }

            // Verify CRC
            if (DB_PACK_HEAD_FLAGS_CRC & head.flags) {
                uint8_t calc_crc = hnd_crc8(s_db_pack_chan->cbuf + DB_PACK_HEAD_SIZE_TOTAL, 
                                           head.length, DB_PACK_CRC8_INIT_VALUE);
                if (calc_crc != head.crc) {
                    LOGW("%s: CRC mismatch, expected=0x%02X, calculated=0x%02X\n", 
                         __func__, head.crc, calc_crc);
                }
            }

            // Complete packet received, call callback
            if (recv_cb) {
                recv_cb(ptr->payload, head.length);
            }

            s_db_pack_chan->ccount = 0; // Reset cache
        }
    }

    return (int)(length - left);
}

int db_pack_get_header_size(void)
{
    return DB_PACK_HEAD_SIZE_TOTAL;
}
