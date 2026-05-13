# `host-tools/` — PC-side helpers

| File | Status | What |
|---|---|---|
| [`shrikevault_proto.py`](shrikevault_proto.py) | ✅ | Wire-protocol client. `WalletClient` class — frame build/parse, CRC, transport. Currently tunnels frames through the `wallet_req <hex>` console command; will swap transparently to TinyUSB CDC-ACM in a later iteration. |
| [`shrikevault_cli.py`](shrikevault_cli.py) | ✅ | Command-line wrapper: `info`, `address <i>`, `sign-digest <i> <hex>`, `sign-eth <i> <hex> [--chain-id N]`. |
| [`eth_signer_demo.py`](eth_signer_demo.py) | ✅ | End-to-end demo: build an EIP-1559 Sepolia tx, sign on device, recover the signer, confirm it matches addr[0]. **The "is this thing actually a working Ethereum signer" smoke test.** |
| [`probe_board.py`](probe_board.py) | ✅ | Lower-level: list serial ports, flag the Shrike-Fi's ESP32-S3 USB Serial/JTAG, poke the bringup-firmware console. |
| `web3_signer.py` | ⚪ planned | `web3.py` external-signer shim — `LocalAccount`-like that delegates signing to a `WalletClient`. Drops into any `web3` script. |

## Setup

```bash
pip install pyserial rlp eth-utils eth-keys eth-account
```

## Quick start

```bash
# Auto-find the board by VID/PID (0x303A/0x1001):
python shrikevault_cli.py info
python shrikevault_cli.py address 0
python shrikevault_cli.py sign-digest 0 e67fb1c86872b253b88f91a99dbfe32f32ff41c47de8fad7ae1a5014468a4b87

# End-to-end EIP-1559 signing + signature-recovery verification:
python eth_signer_demo.py
```

Expected `eth_signer_demo.py` output:
```
OVERALL: [OK] MATCH - the device produced a valid Ethereum signature for the right key.
```

## Wire protocol (mirrors `firmware/shrikevault/components/wallet_proto/`)

```
 byte  field
  0-1  magic       'S','V'  (0x53 0x56)
   2   version     0x01
   3   msg_type    0x01 DEVICE_INFO | 0x02 GET_ADDRESS | 0x03 SIGN_DIGEST | 0x04 SIGN_ETH | 0xFF ERROR
  4-5  payload_len uint16 big-endian
 6-N   payload     payload_len bytes
 last4 crc32       big-endian, zlib.crc32 / esp_crc32_le
```

The Python side's frame builders are in `shrikevault_proto.py`; the C side's
parser is in `firmware/shrikevault/components/wallet_proto/wallet_proto.c`.

## Heads-up on the open-port reset

On `serial.Serial(port).open()`, pyserial pulses DTR/RTS by default, which on
the ESP32-S3's USB-Serial-JTAG **triggers a hardware reset**. We avoid that by
setting `dtr=False` / `rts=False` *before* `open()` — `WalletClient` does this
for you. If you write your own client, do the same or every fresh connection
will race the boot.
