#!/usr/bin/env python3
"""
shrikevault_cli — command-line wrapper around shrikevault_proto.WalletClient.

Examples:
    python shrikevault_cli.py info
    python shrikevault_cli.py address 0
    python shrikevault_cli.py sign-digest 0 e67fb1c86872b253b88f91a99dbfe32f32ff41c47de8fad7ae1a5014468a4b87
    python shrikevault_cli.py sign-eth 0 02f1...  --chain-id 11155111

If --port is omitted, auto-finds the Shrike-Fi by VID/PID (0x303A/0x1001).
"""
from __future__ import annotations

import argparse
import sys

from shrikevault_proto import (
    WalletClient, WalletError,
    DeviceInfo, EthSignature,
)


def _client(args) -> WalletClient:
    port = args.port or WalletClient.find_port()
    if not port:
        sys.exit("no ShrikeVault found on USB. Try --port COMxx (or check the device is plugged in).")
    return WalletClient(port, timeout=args.timeout)


def cmd_info(args) -> int:
    with _client(args) as c:
        info: DeviceInfo = c.device_info()
    print(f"banner             : {info.banner}")
    print(f"firmware version   : {info.fw_major}.{info.fw_minor}")
    print(f"chip target        : {'esp32s3' if info.chip_target == 1 else f'unknown({info.chip_target})'}")
    print(f"device provisioned : {info.provisioned}")
    if info.address_fingerprint:
        print(f"addr[0] fingerprint: {info.address_fingerprint}")
    return 0


def cmd_address(args) -> int:
    with _client(args) as c:
        addr = c.get_address(args.index)
    print(addr)
    return 0


def cmd_sign_digest(args) -> int:
    digest = bytes.fromhex(args.digest.removeprefix("0x"))
    if len(digest) != 32:
        sys.exit("digest must be exactly 32 bytes (64 hex chars)")
    with _client(args) as c:
        sig: EthSignature = c.sign_digest(args.index, digest)
    print(f"r           : 0x{sig.r.hex()}")
    print(f"s           : 0x{sig.s.hex()}")
    print(f"recovery_id : {sig.recovery_id}")
    print(f"raw (r||s||rid, 65 B): 0x{sig.raw.hex()}")
    return 0


def cmd_sign_eth(args) -> int:
    msg = bytes.fromhex(args.message.removeprefix("0x"))
    with _client(args) as c:
        sig: EthSignature = c.sign_eth(args.index, msg)
    print(f"signed keccak256(msg) — msg was {len(msg)} bytes")
    print(f"r           : 0x{sig.r.hex()}")
    print(f"s           : 0x{sig.s.hex()}")
    print(f"recovery_id : {sig.recovery_id}")
    if args.chain_id is not None:
        if args.tx_type == "legacy":
            print(f"v (EIP-155, chain_id={args.chain_id}): {sig.v_legacy_eip155(args.chain_id)}")
        else:
            print(f"v (EIP-1559)                    : {sig.v_eip1559()}")
    print(f"raw (r||s||rid, 65 B): 0x{sig.raw.hex()}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(prog="shrikevault_cli")
    ap.add_argument("--port",    help="serial port (default: auto-find Shrike-Fi by VID/PID 0x303A/0x1001)")
    ap.add_argument("--timeout", type=float, default=5.0, help="per-request timeout in seconds")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="print device info").set_defaults(fn=cmd_info)

    p = sub.add_parser("address", help="derive ETH address at m/44'/60'/0'/0/<index>")
    p.add_argument("index", type=int)
    p.set_defaults(fn=cmd_address)

    p = sub.add_parser("sign-digest", help="sign a 32-byte digest")
    p.add_argument("index", type=int)
    p.add_argument("digest", help="32-byte digest (hex; '0x' prefix optional)")
    p.set_defaults(fn=cmd_sign_digest)

    p = sub.add_parser("sign-eth", help="device keccak256(message) then signs")
    p.add_argument("index", type=int)
    p.add_argument("message", help="message bytes (hex; '0x' prefix optional)")
    p.add_argument("--chain-id", type=int, help="if set, also print the chain-specific v")
    p.add_argument("--tx-type", choices=("legacy", "eip1559"), default="eip1559",
                   help="affects v computation when --chain-id is set (default: eip1559)")
    p.set_defaults(fn=cmd_sign_eth)

    args = ap.parse_args()
    try:
        return args.fn(args)
    except WalletError as e:
        print(f"WalletError: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
