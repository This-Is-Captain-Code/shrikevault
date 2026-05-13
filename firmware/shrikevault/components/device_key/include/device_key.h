/*
 * device_key.h — the wallet's persistent 256-bit root secret.
 *
 * This is the v1 root-of-trust: on first boot we draw 256 bits from the
 * ESP32-S3's hardware RNG, write them to NVS (encrypted in release builds),
 * and use them as the seed for every subsequent BIP-39/BIP-32 derivation.
 * On every later boot we read the same bytes back. The mnemonic is shown
 * exactly once on first init (see bip39_bip32 — that component will call
 * device_key_get and then derive + display).
 *
 * This is intentionally *not* the same as the original PUF design — see
 * docs/threat-model.md for the trade-offs.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define DEVICE_KEY_BYTES   32   /* 256 bits */

/* Initialise the device-key subsystem. Opens NVS. Idempotent. */
esp_err_t device_key_init(void);

/* True iff a device key has been provisioned to NVS (i.e. not first boot). */
bool device_key_provisioned(void);

/* Provision a fresh 256-bit key from the on-chip hardware RNG and persist it
 * to NVS. Fails (ESP_ERR_INVALID_STATE) if a key is already provisioned —
 * call device_key_wipe() first if you really want to rotate.
 *
 * The RNG source is esp_fill_random(), which on ESP32-S3 reads from the
 * dedicated TRNG seeded by RF + on-die thermal noise. We over-draw and stir
 * (HMAC-SHA256) to harden against any single-source weakness, then commit.
 *
 * On success, *out_key (if non-NULL) is filled with the new key. The caller
 * is responsible for wiping that buffer when done. */
esp_err_t device_key_provision(uint8_t out_key[DEVICE_KEY_BYTES]);

/* Read the previously-provisioned key into *out_key. Must already exist
 * (device_key_provisioned() == true), else ESP_ERR_NOT_FOUND. */
esp_err_t device_key_get(uint8_t out_key[DEVICE_KEY_BYTES]);

/* Permanently delete the key from NVS. Destructive — there is no recovery
 * without the BIP-39 mnemonic. Intended for factory-reset / wipe commands. */
esp_err_t device_key_wipe(void);

/* Constant-time-ish wipe of a buffer. (Convenience used by callers handling
 * post-derivation secret scrubbing.) */
void device_key_memzero(void *p, size_t n);
