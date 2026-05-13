/*
 * eth_tx.h — Ethereum transaction signing layer.
 *
 * Thin wrapper over wallet_sign_digest: the device receives an unsigned
 * Ethereum message (the bytes that should be keccak256'd before signing),
 * computes the digest itself, then signs.
 *
 * For each ETH tx type, the *unsigned message* the host passes is:
 *
 *   Legacy / EIP-155      :  rlp([nonce, gasPrice, gasLimit, to, value, data,
 *                                  chainId, 0, 0])
 *   EIP-1559 (type-2)     :  0x02 || rlp([chainId, nonce, maxPriorityFeePerGas,
 *                                          maxFeePerGas, gasLimit, to, value,
 *                                          data, accessList])
 *   EIP-191 personal_sign :  "\x19Ethereum Signed Message:\n" || strlen(msg) || msg
 *
 * The device doesn't need to know which type it is — it just hashes the bytes
 * and signs. The host computes the chain-specific `v` from the returned
 * recovery_id (see wallet_sign_digest's docstring for the formula).
 *
 * Signing is RFC-6979 deterministic ECDSA on secp256k1 (no RNG on the signing
 * path → no nonce-reuse failure mode).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Sign keccak256(msg) with the key at m/44'/60'/0'/0/<key_index>.
 *
 * out_sig[ 0..31]  = r            (big-endian)
 * out_sig[32..63]  = s            (big-endian, low-s per EIP-2)
 * out_sig[64]      = recovery_id  (0 or 1; host computes v from chain rules)
 */
esp_err_t eth_tx_sign(uint32_t key_index, const uint8_t *msg, size_t msg_len, uint8_t out_sig[65]);
