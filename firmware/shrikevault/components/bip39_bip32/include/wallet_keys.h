/*
 * wallet_keys.h — derive the BIP-39 mnemonic and Ethereum keys from the
 * device root key.
 *
 * Flow:
 *   device_key (32 bytes, from device_key component)
 *      └── HKDF-SHA256(key=device_key, info="shrikevault.bip39.v1")
 *          → 32 bytes of BIP-39 entropy
 *              └── mnemonic_from_data(entropy, 32)
 *                  → 24-word English mnemonic           ← shown ONCE on first init
 *                      └── PBKDF2(mnemonic, "mnemonic" + passphrase, 2048, SHA-512)
 *                          → 64-byte BIP-39 seed
 *                              └── hdnode_from_seed(seed, "secp256k1")
 *                                  → BIP-32 master node
 *                                      └── CKDpriv along m/44'/60'/0'/0/index
 *                                          → ETH account private key + public key
 *                                              → address = keccak256(uncompressed_pubkey[1:])[12:]
 *                                              → EIP-55 checksum the hex output
 *
 * Determinism: same device_key → same mnemonic → same addresses, every boot.
 * (This is the whole point: paper-backup the mnemonic at first init, and any
 * BIP-39/BIP-44 wallet can recover the same keys.)
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* The English BIP-39 mnemonic (24 words for 256-bit entropy) is at most
 * 24 words × (8 chars max per word + 1 space) - 1 = 215 chars. Round up. */
#define WALLET_MNEMONIC_MAX   240

/* ETH address as a 0x-prefixed EIP-55-checksummed hex string (42 chars + NUL). */
#define WALLET_ETH_ADDR_LEN   43

/* Derive the 24-word BIP-39 mnemonic from the device_key. Writes a
 * NUL-terminated string into out (must be ≥ WALLET_MNEMONIC_MAX). The caller
 * is responsible for wiping out after use — this function does NOT persist
 * the mnemonic anywhere; it's recomputed each call from the device_key. */
esp_err_t wallet_derive_mnemonic(char out[WALLET_MNEMONIC_MAX]);

/* Derive the ETH address at BIP-44 path m/44'/60'/0'/0/index. Writes a
 * NUL-terminated EIP-55 checksummed "0x..." hex string into out_addr. The
 * private key never leaves this function. */
esp_err_t wallet_get_eth_address(uint32_t index, char out_addr[WALLET_ETH_ADDR_LEN]);

/* Sign a 32-byte digest with the private key at m/44'/60'/0'/0/<index>.
 *
 * Output layout (65 bytes):
 *   out_sig[ 0..31]  r           (big-endian)
 *   out_sig[32..63]  s           (big-endian, low-s normalised per EIP-2)
 *   out_sig[64]      recovery_id (0 or 1)
 *
 * Signing uses RFC-6979 deterministic nonces — same digest + same key always
 * produces the same signature, no RNG on the signing path.
 *
 * The caller turns recovery_id into the chain-specific `v`:
 *   legacy / EIP-155 :  v = chain_id * 2 + 35 + recovery_id
 *   EIP-1559 / 2930  :  v = recovery_id            (i.e. yParity, 0 or 1)
 *   personal_sign    :  v = 27 + recovery_id
 *
 * The private key is derived from the device_key on each call, used, and
 * zeroed before return; it never persists. */
esp_err_t wallet_sign_digest(uint32_t index, const uint8_t digest[32], uint8_t out_sig[65]);
