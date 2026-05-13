/*
 * wallet_proto.c — see wallet_proto.h
 */
#include "wallet_proto.h"

#include <string.h>
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_crc.h"

#include "device_key.h"
#include "wallet_keys.h"
#include "eth_tx.h"

static const char *TAG = "wp";

/* ------------------------- frame helpers ------------------------------- */

static uint32_t crc32_of(const uint8_t *p, size_t n) {
    /* esp_crc32_le is the IEEE-802.3 (Ethernet/PKZIP) CRC-32 — same poly
     * as zlib's crc32; widely available on the host side. */
    return esp_crc32_le(0, p, n);
}

static void put_u16_be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static uint16_t get_u16_be(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static uint32_t get_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* Build a response frame in `rsp` and set *rsp_len_out.
 * Returns ESP_OK if it fits, ESP_ERR_NO_MEM otherwise. */
static esp_err_t build_frame(uint8_t msg_type,
                             const uint8_t *payload, size_t payload_len,
                             uint8_t *rsp, size_t rsp_max, size_t *rsp_len_out)
{
    if (payload_len > WP_PAYLOAD_MAX) return ESP_ERR_INVALID_SIZE;
    size_t need = WP_OVERHEAD + payload_len;
    if (need > rsp_max) return ESP_ERR_NO_MEM;

    rsp[0] = WP_MAGIC0;
    rsp[1] = WP_MAGIC1;
    rsp[2] = WP_VERSION;
    rsp[3] = msg_type;
    put_u16_be(&rsp[4], (uint16_t)payload_len);
    if (payload_len) memcpy(&rsp[WP_HEADER_BYTES], payload, payload_len);

    uint32_t c = crc32_of(rsp, WP_HEADER_BYTES + payload_len);
    put_u32_be(&rsp[WP_HEADER_BYTES + payload_len], c);

    *rsp_len_out = need;
    return ESP_OK;
}

/* Convenience: build an ERROR response (code + ascii reason). */
static esp_err_t err_frame(uint8_t code, const char *reason,
                           uint8_t *rsp, size_t rsp_max, size_t *rsp_len_out)
{
    uint8_t p[1 + 96];
    p[0] = code;
    size_t rl = reason ? strlen(reason) : 0;
    if (rl > sizeof(p) - 1) rl = sizeof(p) - 1;
    if (rl) memcpy(&p[1], reason, rl);
    return build_frame(WP_MSG_ERROR, p, 1 + rl, rsp, rsp_max, rsp_len_out);
}

/* ------------------------- handlers ------------------------------------ */

/* DEVICE_INFO payload (response):
 *   "ShrikeVault\0"                  ~12 bytes  product banner
 *   u8  fw_major
 *   u8  fw_minor
 *   u8  provisioned (0|1)
 *   u8  chip_target               (1=esp32s3)
 *   For provisioned devices, the first 4 + last 4 bytes of address[0] as a
 *   short fingerprint string ("0x3B94...A02d\0") so the host can recognise
 *   this physical device without revealing a full key fingerprint. */
static esp_err_t h_device_info(const uint8_t *req_payload, size_t req_len,
                               uint8_t *rsp, size_t rsp_max, size_t *rsp_len_out)
{
    (void)req_payload; (void)req_len;
    uint8_t p[128];
    size_t o = 0;
    static const char banner[] = "ShrikeVault";
    memcpy(&p[o], banner, sizeof(banner)); o += sizeof(banner);   /* incl. NUL */
    p[o++] = 0;  /* fw major */
    p[o++] = 2;  /* fw minor (P2 in roadmap terms) */
    bool prov = device_key_provisioned();
    p[o++] = prov ? 1 : 0;
    p[o++] = 1;  /* chip target = esp32s3 */

    if (prov) {
        char addr[WALLET_ETH_ADDR_LEN];
        esp_err_t err = wallet_get_eth_address(0, addr);
        if (err == ESP_OK) {
            /* "0x3B94...A02d\0" — 14 chars + NUL */
            const char *full = addr;          /* "0x3B9477FD...02d" */
            char fp[16];
            snprintf(fp, sizeof(fp), "%.6s...%.4s", full, full + strlen(full) - 4);
            size_t fl = strlen(fp) + 1;
            if (o + fl <= sizeof(p)) { memcpy(&p[o], fp, fl); o += fl; }
        }
    }
    return build_frame(WP_MSG_DEVICE_INFO, p, o, rsp, rsp_max, rsp_len_out);
}

/* GET_ADDRESS payload: u32 index (BE). Response: 42-byte EIP-55 string + NUL. */
static esp_err_t h_get_address(const uint8_t *p, size_t plen,
                               uint8_t *rsp, size_t rsp_max, size_t *rsp_len_out)
{
    if (plen != 4) return err_frame(WP_ERR_BAD_PAYLOAD, "GET_ADDRESS payload must be 4 bytes", rsp, rsp_max, rsp_len_out);
    if (!device_key_provisioned()) return err_frame(WP_ERR_NO_DEVICE_KEY, "no device key", rsp, rsp_max, rsp_len_out);
    uint32_t idx = get_u32_be(p);
    char addr[WALLET_ETH_ADDR_LEN];
    esp_err_t err = wallet_get_eth_address(idx, addr);
    if (err != ESP_OK) return err_frame(WP_ERR_INTERNAL, esp_err_to_name(err), rsp, rsp_max, rsp_len_out);
    return build_frame(WP_MSG_GET_ADDRESS, (const uint8_t *)addr, strlen(addr) + 1, rsp, rsp_max, rsp_len_out);
}

/* SIGN_DIGEST payload: u32 index + 32 bytes digest. Response: r||s||rid (65 B). */
static esp_err_t h_sign_digest(const uint8_t *p, size_t plen,
                               uint8_t *rsp, size_t rsp_max, size_t *rsp_len_out)
{
    if (plen != 4 + 32) return err_frame(WP_ERR_BAD_PAYLOAD, "SIGN_DIGEST payload must be 4+32 bytes", rsp, rsp_max, rsp_len_out);
    if (!device_key_provisioned()) return err_frame(WP_ERR_NO_DEVICE_KEY, "no device key", rsp, rsp_max, rsp_len_out);
    uint32_t idx = get_u32_be(p);
    uint8_t sig[65];
    esp_err_t err = wallet_sign_digest(idx, p + 4, sig);
    if (err != ESP_OK) return err_frame(WP_ERR_INTERNAL, esp_err_to_name(err), rsp, rsp_max, rsp_len_out);
    return build_frame(WP_MSG_SIGN_DIGEST, sig, sizeof(sig), rsp, rsp_max, rsp_len_out);
}

/* SIGN_ETH payload: u32 index + N bytes message-to-keccak. Response: 65 B. */
static esp_err_t h_sign_eth(const uint8_t *p, size_t plen,
                            uint8_t *rsp, size_t rsp_max, size_t *rsp_len_out)
{
    if (plen < 4) return err_frame(WP_ERR_BAD_PAYLOAD, "SIGN_ETH payload < 4 bytes", rsp, rsp_max, rsp_len_out);
    if (!device_key_provisioned()) return err_frame(WP_ERR_NO_DEVICE_KEY, "no device key", rsp, rsp_max, rsp_len_out);
    uint32_t idx = get_u32_be(p);
    uint8_t sig[65];
    esp_err_t err = eth_tx_sign(idx, p + 4, plen - 4, sig);
    if (err != ESP_OK) return err_frame(WP_ERR_INTERNAL, esp_err_to_name(err), rsp, rsp_max, rsp_len_out);
    return build_frame(WP_MSG_SIGN_ETH, sig, sizeof(sig), rsp, rsp_max, rsp_len_out);
}

/* ------------------------- dispatch ------------------------------------ */

esp_err_t wallet_proto_dispatch(const uint8_t *req, size_t req_len,
                                uint8_t *rsp, size_t rsp_max, size_t *rsp_len_out)
{
    if (!req || !rsp || !rsp_len_out) return ESP_ERR_INVALID_ARG;
    if (rsp_max < WP_OVERHEAD + 96) return ESP_ERR_INVALID_ARG;  /* room for the worst ERROR frame */

    if (req_len < WP_OVERHEAD)               return err_frame(WP_ERR_BAD_LENGTH,  "frame too short",  rsp, rsp_max, rsp_len_out);
    if (req[0] != WP_MAGIC0 || req[1] != WP_MAGIC1)
                                              return err_frame(WP_ERR_BAD_MAGIC,   "bad magic",         rsp, rsp_max, rsp_len_out);
    if (req[2] != WP_VERSION)                return err_frame(WP_ERR_BAD_VERSION, "version != 1",      rsp, rsp_max, rsp_len_out);

    uint16_t plen = get_u16_be(&req[4]);
    if ((size_t)plen > WP_PAYLOAD_MAX)       return err_frame(WP_ERR_BAD_LENGTH,  "payload too big",   rsp, rsp_max, rsp_len_out);
    if (req_len != WP_OVERHEAD + (size_t)plen)
                                              return err_frame(WP_ERR_BAD_LENGTH,  "length mismatch",   rsp, rsp_max, rsp_len_out);

    uint32_t want = crc32_of(req, WP_HEADER_BYTES + plen);
    uint32_t got  = get_u32_be(&req[WP_HEADER_BYTES + plen]);
    if (want != got) {
        ESP_LOGW(TAG, "bad CRC: want 0x%08lx got 0x%08lx", (unsigned long)want, (unsigned long)got);
        return err_frame(WP_ERR_BAD_CRC, "crc mismatch", rsp, rsp_max, rsp_len_out);
    }

    const uint8_t  msg_type = req[3];
    const uint8_t *payload  = &req[WP_HEADER_BYTES];

    switch (msg_type) {
    case WP_MSG_DEVICE_INFO:  return h_device_info (payload, plen, rsp, rsp_max, rsp_len_out);
    case WP_MSG_GET_ADDRESS:  return h_get_address (payload, plen, rsp, rsp_max, rsp_len_out);
    case WP_MSG_SIGN_DIGEST:  return h_sign_digest (payload, plen, rsp, rsp_max, rsp_len_out);
    case WP_MSG_SIGN_ETH:     return h_sign_eth    (payload, plen, rsp, rsp_max, rsp_len_out);
    default:                  return err_frame     (WP_ERR_UNKNOWN_MSG, "unknown msg_type", rsp, rsp_max, rsp_len_out);
    }
}
