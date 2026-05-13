/*
 * wallet_proto.h — request/response wire protocol for ShrikeVault.
 *
 * Frame format (request and response):
 *
 *   byte  field          notes
 *   0..1  magic          0x53 0x56  ('S','V')
 *   2     version        0x01 in v1
 *   3     msg_type       see WP_MSG_*
 *   4..5  payload_len    big-endian uint16, ≤ WP_PAYLOAD_MAX
 *   6..   payload        payload_len bytes
 *   N-4   crc32          big-endian, IEEE-802.3 (esp_crc32_le), over all
 *                          preceding bytes (header + payload)
 *
 * v1 message types (req → rsp):
 *   0x01 DEVICE_INFO   ()                              → (chip, fw_ver, provisioned, addr_at_index_0?)
 *   0x02 GET_ADDRESS   (u32 index)                     → ("0x..." EIP-55, 42 bytes)
 *   0x03 SIGN_DIGEST   (u32 index, 32B digest)         → (r||s||rid, 65 bytes)
 *   0x04 SIGN_ETH      (u32 index, N bytes msg)        → (r||s||rid, 65 bytes)
 *   0xFF ERROR         response only                    → (u8 code, ASCII reason)
 *
 * All multi-byte integers in payloads are **big-endian**. Indices and lengths
 * use the natural width stated in the message-type docs above.
 *
 * Same wire protocol will run unchanged over TinyUSB CDC-ACM once we swap
 * the transport in a later iteration. For now it's tunnelled through the
 * USB-Serial-JTAG console via the `wallet_req <hex>` command.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define WP_MAGIC0          0x53   /* 'S' */
#define WP_MAGIC1          0x56   /* 'V' */
#define WP_VERSION         0x01
#define WP_HEADER_BYTES    6      /* magic(2) + ver(1) + type(1) + len(2)   */
#define WP_TRAILER_BYTES   4      /* crc32                                  */
#define WP_OVERHEAD        (WP_HEADER_BYTES + WP_TRAILER_BYTES)
#define WP_PAYLOAD_MAX     4096   /* fits the biggest unsigned-tx + headroom */
#define WP_FRAME_MAX       (WP_PAYLOAD_MAX + WP_OVERHEAD)

/* Message types. */
#define WP_MSG_DEVICE_INFO   0x01
#define WP_MSG_GET_ADDRESS   0x02
#define WP_MSG_SIGN_DIGEST   0x03
#define WP_MSG_SIGN_ETH      0x04
#define WP_MSG_ERROR         0xFF

/* Error codes (used as the first payload byte of an ERROR response). */
#define WP_ERR_BAD_MAGIC       0x01
#define WP_ERR_BAD_VERSION     0x02
#define WP_ERR_BAD_LENGTH      0x03
#define WP_ERR_BAD_CRC         0x04
#define WP_ERR_UNKNOWN_MSG     0x05
#define WP_ERR_BAD_PAYLOAD     0x06
#define WP_ERR_NO_DEVICE_KEY   0x07
#define WP_ERR_INTERNAL        0xFE

/* Consume `req` (a complete frame), produce a response frame into `rsp`.
 * On success, *rsp_len_out is set to the response frame length.
 * On framing errors (bad magic / CRC / etc.) we still produce a well-formed
 * ERROR response frame and return ESP_OK — the caller writes it back to the
 * peer. ESP_ERR_INVALID_ARG / ESP_ERR_NO_MEM only happen for caller bugs. */
esp_err_t wallet_proto_dispatch(const uint8_t *req, size_t req_len,
                                uint8_t *rsp,       size_t rsp_max,
                                size_t  *rsp_len_out);
