# `shrikevault` — the wallet firmware (ESP-IDF)

> This is the wallet itself, separate from [`../bringup/`](../bringup/) which
> was a P0 board bring-up smoke test.

| Build status | What works |
|---|---|
| 🟡 P1 | **device_key**: first-init HW-RNG-generated 256-bit root + NVS persistence + read/wipe |
| coming | bip39_bip32: derive BIP-39 mnemonic + BIP-32 master from device_key |
| coming | eth_tx: secp256k1 + Keccak + RLP + EIP-1559 / EIP-155 / EIP-55 + calldata decode |
| coming | wallet_transport: TinyUSB CDC-ACM framed protocol + on-device confirm gate |

## Console commands (P1)

| | |
|---|---|
| `info` | wallet + build info |
| `dk_status` | is a device key provisioned? show a short fingerprint (first/last 4 bytes) |
| `dk_provision` | first-init: create the 256-bit device key from the on-chip TRNG |
| `dk_wipe CONFIRM` | **destructive** — erase the device key |

## Build & flash

```powershell
& "$env:USERPROFILE\esp\esp-idf\export.ps1"
idf.py -C firmware/shrikevault set-target esp32s3
idf.py -C firmware/shrikevault build
idf.py -C firmware/shrikevault -p COM19 flash monitor
```

## Project layout

```
firmware/shrikevault/
├── CMakeLists.txt
├── sdkconfig.defaults          target=esp32s3, 8 MB flash, USB-Serial-JTAG console, no PSRAM, BT off
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  app entry, console, dk_* commands
├── components/
│   └── device_key/             v1 root-of-trust: HW-RNG-generated 256-bit secret in NVS
│       ├── CMakeLists.txt
│       ├── include/device_key.h
│       └── device_key.c
└── README.md (this file)
```

## Security note

This is the **DEBUG** profile — no Secure Boot v2, no Flash Encryption, no
encrypted NVS. The device_key sits on flash in plaintext. **Do not use this
build with real funds.** The release profile (P4) enables the full security
stack — see [`../../docs/threat-model.md`](../../docs/threat-model.md).
