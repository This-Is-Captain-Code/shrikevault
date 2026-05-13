/*
 * eth_tx.c — see eth_tx.h
 */
#include "eth_tx.h"

#include <string.h>
#include "esp_log.h"

#include "wallet_keys.h"
#include "sha3.h"
#include "memzero.h"

static const char *TAG = "eth_tx";

esp_err_t eth_tx_sign(uint32_t key_index, const uint8_t *msg, size_t msg_len, uint8_t out_sig[65])
{
    if (!msg || !out_sig) return ESP_ERR_INVALID_ARG;

    uint8_t digest[32];
    keccak_256(msg, msg_len, digest);

    esp_err_t err = wallet_sign_digest(key_index, digest, out_sig);
    memzero(digest, sizeof(digest));

    if (err != ESP_OK) ESP_LOGE(TAG, "wallet_sign_digest: %s", esp_err_to_name(err));
    return err;
}
