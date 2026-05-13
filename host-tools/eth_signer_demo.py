#!/usr/bin/env python3
"""
eth_signer_demo — build an unsigned EIP-1559 transaction, sign it on the
ShrikeVault device, recover the signer, confirm it equals addr[0].

This is the end-to-end "the device is a real Ethereum signer" smoke test.
It does NOT broadcast (no network needed) — only verifies the signature is
valid and produced by the right key.

Usage:    python eth_signer_demo.py [--port COMxx] [--index 0]
"""
from __future__ import annotations

import argparse
import sys

from shrikevault_proto import WalletClient

try:
    import rlp
    from eth_utils import keccak, to_checksum_address
    from eth_keys import keys
except ImportError as e:
    sys.exit("required:  pip install rlp eth-utils eth-keys eth-account")


def rlp_encode_eip1559_unsigned(chain_id, nonce, max_priority, max_fee, gas, to, value, data, access_list=None):
    """Build the 0x02 || rlp([...]) preimage that EIP-1559 signs."""
    payload = [
        chain_id,
        nonce,
        max_priority,
        max_fee,
        gas,
        bytes.fromhex(to[2:]) if to else b"",
        value,
        data,
        access_list or [],
    ]
    return b"\x02" + rlp.encode(payload)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="serial port (default: auto-find)")
    ap.add_argument("--index", type=int, default=0)
    args = ap.parse_args()

    port = args.port or WalletClient.find_port()
    if not port:
        sys.exit("no Shrike-Fi found on USB")

    with WalletClient(port) as c:
        info = c.device_info()
        print(f"device: {info.banner} v{info.fw_major}.{info.fw_minor}, addr[0] fingerprint = {info.address_fingerprint}")
        addr = c.get_address(args.index)
        print(f"addr[{args.index}]: {addr}")

        # An EIP-1559 testnet transfer (Sepolia, chain_id=11155111) sending
        # 0.001 ETH to a known dead-letter address. Numbers are arbitrary but
        # plausible — we're not broadcasting, just signing.
        tx = dict(
            chain_id     = 11155111,                   # Sepolia
            nonce        = 0,
            max_priority = 1_000_000_000,              # 1 gwei
            max_fee      = 30_000_000_000,             # 30 gwei
            gas          = 21000,
            to           = "0x000000000000000000000000000000000000dEaD",
            value        = 1_000_000_000_000_000,      # 0.001 ETH (wei)
            data         = b"",
        )
        print()
        print(f"tx        : EIP-1559 transfer  {tx['value']/1e18:.4f} ETH  ->  {tx['to']}  on chain {tx['chain_id']}")

        unsigned = rlp_encode_eip1559_unsigned(**tx)
        msg_hash = keccak(unsigned)
        print(f"keccak    : 0x{msg_hash.hex()}   (device will compute this from the {len(unsigned)}-byte message)")

        print()
        print("--- signing on device ---")
        sig = c.sign_eth(args.index, unsigned)
        print(f"r         : 0x{sig.r.hex()}")
        print(f"s         : 0x{sig.s.hex()}")
        print(f"y_parity  : {sig.recovery_id}      (EIP-1559 v = y_parity)")

        # Recover the signer from (msg_hash, sig) and check.
        rs_sig = keys.Signature(vrs=(sig.recovery_id, int.from_bytes(sig.r, "big"), int.from_bytes(sig.s, "big")))
        recovered_pub = rs_sig.recover_public_key_from_msg_hash(msg_hash)
        recovered = to_checksum_address(recovered_pub.to_address())
        print()
        print(f"recovered : {recovered}")
        print(f"expected  : {addr}")
        match = recovered.lower() == addr.lower()
        print()
        print("OVERALL: " + ("[OK] MATCH - the device produced a valid Ethereum signature for the right key."
                              if match else "[FAIL] MISMATCH - investigate"))
        return 0 if match else 1


if __name__ == "__main__":
    sys.exit(main())
