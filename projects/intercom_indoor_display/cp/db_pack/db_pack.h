#pragma once

#include <common/bk_include.h>
#include <os/os.h>
#include <components/log.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DB_PACK_HEAD_MAGIC_CODE             (0xF0D5)
#define DB_PACK_HEAD_FLAGS_CRC              (1 << 0)
#define DB_PACK_CRC8_INIT_VALUE             (0xFF)

// Endian conversion macros (same as in ntwk_pack)
#define DB_PACK_CHECK_ENDIAN_UINT16
#define DB_PACK_CHECK_ENDIAN_UINT32

typedef struct
{
	uint16_t magic;         /**<  Magic code  2 bytes */
	uint16_t flags;         /**<  Flags  2 bytes */
	uint32_t timestamp;     /**<  Timestamp  4 bytes */
	uint16_t sequence;      /**<  Sequence  2 bytes */
	uint16_t length;        /**<  Length  2 bytes */
	uint8_t crc;            /**<  CRC  1 byte */
	uint8_t reserved[3];    /**<  RESERVED  3 bytes */
	uint8_t  payload[];     /**<  Payload  variable length */
} __attribute__((__packed__)) db_pack_head_t;

typedef struct
{
	uint8_t *cbuf;          /**< Cache buffer for receive */
	uint16_t csize;         /**< Cache buffer size */
	uint16_t ccount;        /**< Current cache count */
	uint16_t sequence;      /**< Sequence number */
	uint8_t *tbuf;          /**< Transmit buffer (with header) */
	uint16_t tsize;         /**< Transmit buffer size (payload only) */
} db_pack_chan_t;

#define DB_PACK_HEAD_SIZE_TOTAL             (sizeof(db_pack_head_t))

/**
 * @brief Receive callback function type
 * @param data Unpacked data (payload only)
 * @param length Payload length
 */
typedef void (*db_pack_recv_cb_t)(uint8_t *data, uint32_t length);

/**
 * @brief Initialize db_pack module
 * @param max_rx_size Maximum receive buffer size (for payload)
 * @param max_tx_size Maximum transmit buffer size (for payload)
 * @return bk_err_t BK_OK on success, error code on failure
 */
bk_err_t db_pack_init(uint16_t max_rx_size, uint16_t max_tx_size);

/**
 * @brief Deinitialize db_pack module
 * @return bk_err_t BK_OK on success, error code on failure
 */
bk_err_t db_pack_deinit(void);

/**
 * @brief Pack data (add header)
 * @param data Data to pack (payload)
 * @param length Data length
 * @param pack_ptr Output pointer to packed data (with header)
 * @param pack_ptr_length Output length of packed data
 * @return int Packed data length on success, negative on failure
 */
int db_pack_pack(uint8_t *data, uint32_t length, uint8_t **pack_ptr, uint32_t *pack_ptr_length);

/**
 * @brief Unpack data (remove header, handle fragmentation)
 * @param data Packed data to unpack
 * @param length Packed data length
 * @param recv_cb Callback function when a complete packet is received
 * @return int Processed data length on success, negative on failure
 */
int db_pack_unpack(uint8_t *data, uint32_t length, db_pack_recv_cb_t recv_cb);

/**
 * @brief Get header size
 * @return int Header size in bytes
 */
int db_pack_get_header_size(void);

#ifdef __cplusplus
}
#endif
