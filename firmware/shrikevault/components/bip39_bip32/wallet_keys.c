/*
 * wallet_keys.c — see wallet_keys.h
 */
#include "wallet_keys.h"

#include <string.h>
#include "esp_log.h"
#include "device_key.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

/* trezor-crypto */
#include "bip39.h"
#include "bip32.h"
#include "curves.h"
#include "secp256k1.h"
#include "sha3.h"
#include "memzero.h"

static const char *TAG = "wallet_keys";

#define BIP39_ENTROPY_BYTES   32   /* 256 bits → 24-word mnemonic */
#define BIP39_SEED_BYTES      64   /* PBKDF2-SHA512 output */
#define ETH_ADDR_BYTES        20

/* Derive BIP-39 entropy from the device key via HKDF-SHA256.
 * info string is versioned so we can rotate the derivation rule later. */
static esp_err_t derive_bip39_entropy(uint8_t out[BIP39_ENTROPY_BYTES])
{
    uint8_t device_key[DEVICE_KEY_BYTES];
    esp_err_t err = device_key_get(device_key);
    if (err != ESP_OK) { ESP_LOGE(TAG, "device_key_get: %s", esp_err_to_name(err)); return err; }

    static const uint8_t info[] = "shrikevault.bip39.v1";
    /* Use an empty salt; the device_key already has full 256-bit entropy.
     * (HKDF-Expand alone would suffice, but the full HKDF API is what mbedtls exposes.) */
    int r = mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                         NULL, 0,                          /* salt */
                         device_key, DEVICE_KEY_BYTES,     /* ikm */
                         info, sizeof(info) - 1,           /* info */
                         out, BIP39_ENTROPY_BYTES);        /* okm */
    device_key_memzero(device_key, sizeof(device_key));
    if (r != 0) { ESP_LOGE(TAG, "mbedtls_hkdf: -0x%04x", -r); return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t wallet_derive_mnemonic(char out[WALLET_MNEMONIC_MAX])
{
    if (!out) return ESP_ERR_INVALID_ARG;

    uint8_t entropy[BIP39_ENTROPY_BYTES];
    esp_err_t err = derive_bip39_entropy(entropy);
    if (err != ESP_OK) return err;

    const char *m = mnemonic_from_data(entropy, sizeof(entropy));
    device_key_memzero(entropy, sizeof(entropy));
    if (!m) { ESP_LOGE(TAG, "mnemonic_from_data returned NULL"); return ESP_FAIL; }

    size_t len = strlen(m);
    if (len >= WALLET_MNEMONIC_MAX) {
        memzero(out, WALLET_MNEMONIC_MAX);
        mnemonic_clear();
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out, m, len);
    out[len] = '\0';

    /* trezor-crypto keeps the mnemonic in a static buffer until mnemonic_clear(). */
    mnemonic_clear();
    return ESP_OK;
}

/* EIP-55 checksum: take lowercase hex, uppercase the hex digit if the
 * corresponding nibble of keccak256(lowercase_hex_without_0x) is ≥ 8. */
static void eip55_checksum(const uint8_t addr[ETH_ADDR_BYTES], char out[WALLET_ETH_ADDR_LEN])
{
    static const char hex[] = "0123456789abcdef";
    char lower[ETH_ADDR_BYTES * 2 + 1];
    for (int i = 0; i < ETH_ADDR_BYTES; i++) {
        lower[2 * i]     = hex[(addr[i] >> 4) & 0xF];
        lower[2 * i + 1] = hex[addr[i] & 0xF];
    }
    lower[ETH_ADDR_BYTES * 2] = '\0';

    uint8_t hash[32];
    keccak_256((const uint8_t *)lower, ETH_ADDR_BYTES * 2, hash);

    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < ETH_ADDR_BYTES * 2; i++) {
        char c = lower[i];
        if (c >= 'a' && c <= 'f') {
            uint8_t nib = (i % 2 == 0) ? (hash[i / 2] >> 4) : (hash[i / 2] & 0xF);
            if (nib >= 8) c = (char)(c - 'a' + 'A');
        }
        out[2 + i] = c;
    }
    out[2 + ETH_ADDR_BYTES * 2] = '\0';
    memzero(lower, sizeof(lower));
    memzero(hash, sizeof(hash));
}

esp_err_t wallet_get_eth_address(uint32_t index, char out_addr[WALLET_ETH_ADDR_LEN])
{
    if (!out_addr) return ESP_ERR_INVALID_ARG;

    /* 1. mnemonic. */
    char mnemonic[WALLET_MNEMONIC_MAX];
    esp_err_t err = wallet_derive_mnemonic(mnemonic);
    if (err != ESP_OK) return err;

    /* 2. seed (BIP-39): PBKDF2-HMAC-SHA512(mnemonic, "mnemonic" + passphrase, 2048).
     *    No user passphrase in v1 (decision locked). */
    uint8_t seed[BIP39_SEED_BYTES];
    mnemonic_to_seed(mnemonic, "", seed, NULL);
    memzero(mnemonic, sizeof(mnemonic));

    /* 3. BIP-32 master + derivation: m / 44' / 60' / 0' / 0 / index. */
    HDNode node;
    if (hdnode_from_seed(seed, sizeof(seed), SECP256K1_NAME, &node) != 1) {
        memzero(seed, sizeof(seed));
        memzero(&node, sizeof(node));
        ESP_LOGE(TAG, "hdnode_from_seed failed");
        return ESP_FAIL;
    }
    memzero(seed, sizeof(seed));

    const uint32_t path[] = { 44u | 0x80000000u, 60u | 0x80000000u, 0u | 0x80000000u, 0u, index };
    for (size_t i = 0; i < sizeof(path) / sizeof(path[0]); i++) {
        if (hdnode_private_ckd(&node, path[i]) != 1) {
            memzero(&node, sizeof(node));
            ESP_LOGE(TAG, "hdnode_private_ckd failed at path[%u] = 0x%08lx",
                     (unsigned)i, (unsigned long)path[i]);
            return ESP_FAIL;
        }
    }

    /* 4. ETH address = last 20 bytes of keccak256(uncompressed_pubkey[1:]). */
    uint8_t addr20[ETH_ADDR_BYTES];
    hdnode_get_ethereum_pubkeyhash(&node, addr20);
    memzero(&node, sizeof(node));

    eip55_checksum(addr20, out_addr);
    memzero(addr20, sizeof(addr20));
    return ESP_OK;
}
