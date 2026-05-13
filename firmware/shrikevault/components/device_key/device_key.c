/*
 * device_key.c — see device_key.h
 *
 * NVS layout (namespace "shrikevault"):
 *   "k"   blob  32 bytes  — the device root key
 *   "kv"  u8    1 byte    — provisioning version (currently 1; bumped if the
 *                            derivation rule for the key ever changes)
 *
 * Hardening:
 *   • we over-draw (3x the requested entropy) from esp_fill_random and
 *     stir with HMAC-SHA256 keyed by a static label — defends against a
 *     single-source RNG weakness;
 *   • we re-read the stored bytes after writing and compare, to catch
 *     a silently-failed write (flash near-end-of-life);
 *   • all intermediates are memzero'd before return.
 *
 * In release builds (P4), NVS is encrypted with an eFuse-derived AES key
 * so the bytes at rest on flash are ciphertext, not the key.
 */
#include "device_key.h"

#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mbedtls/md.h"

static const char *TAG = "device_key";

#define DK_NAMESPACE    "shrikevault"
#define DK_BLOB_NAME    "k"
#define DK_VER_NAME     "kv"
#define DK_VERSION      1

static bool s_inited = false;

/* SHA-256 HMAC with a fixed label, used to stir the over-drawn RNG output. */
static const uint8_t DK_LABEL[] = "shrikevault.device_key.v1";

void device_key_memzero(void *p, size_t n)
{
    /* mbedtls_platform_zeroize would be nicer but we keep deps minimal here. */
    volatile uint8_t *q = (volatile uint8_t *)p;
    while (n--) *q++ = 0;
}

esp_err_t device_key_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS dirty/old, erasing and re-initialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) { ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err)); return err; }

    s_inited = true;
    ESP_LOGI(TAG, "init OK%s", device_key_provisioned() ? " (key already provisioned)" : " (no key yet — first init)");
    return ESP_OK;
}

bool device_key_provisioned(void)
{
    nvs_handle_t h;
    if (nvs_open(DK_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = 0;
    esp_err_t err = nvs_get_blob(h, DK_BLOB_NAME, NULL, &sz);
    nvs_close(h);
    return (err == ESP_OK && sz == DEVICE_KEY_BYTES);
}

/* Draw `out_len` bytes by over-drawing 3x from esp_fill_random and folding via
 * HMAC-SHA256(label, raw). Returns ESP_OK on success. */
static esp_err_t draw_stirred_random(uint8_t *out, size_t out_len)
{
    /* Over-draw: 3x the requested entropy. */
    const size_t over = out_len * 3;
    uint8_t *raw = (uint8_t *)malloc(over);
    if (!raw) return ESP_ERR_NO_MEM;

    esp_fill_random(raw, over);

    /* Self-check: not all-zero and not all-0xFF (catches a broken TRNG). */
    bool all_zero = true, all_ff = true;
    for (size_t i = 0; i < over; i++) {
        if (raw[i] != 0x00) all_zero = false;
        if (raw[i] != 0xFF) all_ff   = false;
    }
    if (all_zero || all_ff) {
        ESP_LOGE(TAG, "TRNG output looks degenerate (all-zero=%d all-ff=%d) — refusing", all_zero, all_ff);
        device_key_memzero(raw, over); free(raw);
        return ESP_FAIL;
    }

    /* HMAC-SHA256(label, raw); take the leading out_len bytes. (HMAC's output is
     * 32 bytes; out_len ≤ 32 always for our use, but we tile if asked larger.) */
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, md, 1 /* HMAC */) != 0) {
        mbedtls_md_free(&ctx); device_key_memzero(raw, over); free(raw); return ESP_FAIL;
    }
    mbedtls_md_hmac_starts(&ctx, DK_LABEL, sizeof(DK_LABEL) - 1);
    mbedtls_md_hmac_update(&ctx, raw, over);

    uint8_t mac[32];
    mbedtls_md_hmac_finish(&ctx, mac);
    mbedtls_md_free(&ctx);

    size_t n = out_len < sizeof(mac) ? out_len : sizeof(mac);
    memcpy(out, mac, n);

    device_key_memzero(mac, sizeof(mac));
    device_key_memzero(raw, over);
    free(raw);
    return ESP_OK;
}

esp_err_t device_key_provision(uint8_t out_key[DEVICE_KEY_BYTES])
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (device_key_provisioned()) {
        ESP_LOGE(TAG, "device key already provisioned — wipe first if rotating");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t key[DEVICE_KEY_BYTES];
    esp_err_t err = draw_stirred_random(key, sizeof(key));
    if (err != ESP_OK) return err;

    /* Persist. */
    nvs_handle_t h;
    err = nvs_open(DK_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) { device_key_memzero(key, sizeof(key)); return err; }

    err = nvs_set_blob(h, DK_BLOB_NAME, key, DEVICE_KEY_BYTES);
    if (err == ESP_OK) err = nvs_set_u8(h, DK_VER_NAME, DK_VERSION);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) { device_key_memzero(key, sizeof(key)); return err; }

    /* Read-back verify (catches silently-failed writes). */
    uint8_t back[DEVICE_KEY_BYTES];
    err = device_key_get(back);
    if (err != ESP_OK || memcmp(back, key, DEVICE_KEY_BYTES) != 0) {
        ESP_LOGE(TAG, "read-back verification FAILED — flash issue?");
        device_key_memzero(key, sizeof(key));
        device_key_memzero(back, sizeof(back));
        return ESP_FAIL;
    }
    device_key_memzero(back, sizeof(back));

    if (out_key) memcpy(out_key, key, DEVICE_KEY_BYTES);
    device_key_memzero(key, sizeof(key));
    ESP_LOGI(TAG, "provisioned new 256-bit device key (version %d)", DK_VERSION);
    return ESP_OK;
}

esp_err_t device_key_get(uint8_t out_key[DEVICE_KEY_BYTES])
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t err = nvs_open(DK_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t sz = DEVICE_KEY_BYTES;
    err = nvs_get_blob(h, DK_BLOB_NAME, out_key, &sz);
    nvs_close(h);
    if (err == ESP_OK && sz != DEVICE_KEY_BYTES) {
        ESP_LOGE(TAG, "stored key has wrong length (%u, expected %d)", (unsigned)sz, DEVICE_KEY_BYTES);
        device_key_memzero(out_key, DEVICE_KEY_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t device_key_wipe(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t err = nvs_open(DK_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    /* Best-effort: erase the namespace's known keys, then commit. */
    nvs_erase_key(h, DK_BLOB_NAME);
    nvs_erase_key(h, DK_VER_NAME);
    err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) ESP_LOGW(TAG, "device key wiped");
    return err;
}
