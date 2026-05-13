/*
 * shrike_random.c — provide trezor-crypto's `random_buffer` from the
 * ESP32-S3 hardware RNG.
 *
 * trezor-crypto declares `random_buffer` in rand.h but does not implement it;
 * the application is expected to provide one backed by a real entropy source.
 * On ESP32-S3 the on-die TRNG is exposed via `esp_fill_random`, which is what
 * we wire through here.
 *
 * NOTE: this is used by the trezor-crypto helpers (random_uniform,
 * random_permute) and as a fallback in some paths. The signing path uses
 * RFC-6979 deterministic ECDSA — no RNG — so a weak `random_buffer` cannot
 * leak signature keys via nonce reuse. We still over-source from the TRNG
 * (same hardening as device_key) to harden everything else built on top.
 */
#include <stdint.h>
#include <stddef.h>
#include "esp_random.h"

void random_buffer(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
}
