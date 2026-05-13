# ShrikeVault — an open-source Ethereum cold wallet on the Shrike-Fi board

> ⚠️ **Status: pre-alpha, do not use with real funds.** This is an in-progress
> engineering project. Nothing here has been audited. See
> [`docs/threat-model.md`](docs/threat-model.md) for an honest account of what
> security properties this design does and does **not** provide.

ShrikeVault is a compact, fully open-source Ethereum hardware wallet (cold
storage signer) built on the [Vicharak Shrike-Fi](https://store.vicharak.in/?product=v002743)
— a ~$9 open-hardware dev board with an **Espressif ESP32-S3** MCU and a
**Renesas SLG47910 "ForgeFPGA"** (1120 × 5-input LUTs).

## The idea in one paragraph

The ESP32-S3 runs the wallet firmware: it derives a **256-bit device root
secret** at first init from the on-chip **hardware RNG**, persists it in
encrypted NVS (with **Flash Encryption** + **Secure Boot v2** in release
builds), and uses it to seed a standard **BIP-39 / BIP-32** hierarchical key
derivation for Ethereum. The mnemonic is shown **once** on first init so the
user can write it down as their recovery backup; from then on it's a standard
recoverable HD wallet. The firmware speaks an **EIP-1559 / legacy** Ethereum
transaction signing protocol over **USB-CDC**, deterministically (RFC-6979)
signs `secp256k1` ECDSA, and decodes/displays plain transfers and ERC-20
`transfer`/`approve` calls on-device (anything else is flagged as
"unknown contract interaction — confirm only if you know what this is").

> **Note on the FPGA.** Earlier iterations of this design used the ForgeFPGA
> as the root of trust via a Physical Unclonable Function. We dropped that for
> v1 — the SLG47910's toolchain (ForgeFPGA Workshop) is GUI-only with no
> scriptable build path, which is incompatible with the reproducible-build /
> hands-off-development workflow this project wants. The PUF is on the
> roadmap as a **post-v1 enhancement** (see [`docs/roadmap.md`](docs/roadmap.md)
> P6); the early scaffolding for it lives under [`fpga/`](fpga/) for whoever
> wants to pick it back up.

## What's in this repo

| Path | What |
|---|---|
| [`docs/`](docs/) | Architecture, threat model, roadmap, hardware notes (read these first) |
| [`firmware/bringup/`](firmware/bringup/) | P0 board bring-up app (ESP-IDF) — proves the ESP32 ↔ FPGA link works over USB. Still useful for debugging hardware. |
| [`firmware/shrikevault/`](firmware/shrikevault/) | **The wallet itself** (ESP-IDF) — device key, BIP-39/32, secp256k1, Keccak, RLP, EIP-1559, USB-CDC transport |
| [`host-tools/`](host-tools/) | PC-side helpers — serial probe; later: the signing client + web3.py shim |
| [`fpga/`](fpga/) | (deferred) RO-PUF Verilog + smoke-test design — kept for the future PUF revival |

## Hardware

- **Board:** Vicharak Shrike-Fi (ESP32-S3 + Renesas SLG47910 ForgeFPGA, 8 MB QSPI flash, 2 user LEDs, USB-C). Base version has **no PSRAM** (optional add-on).
- **Host channel:** USB-CDC over the existing USB-C connector. Not a true air-gap (it's a wire) — see the threat model.
- **Coins:** Ethereum only for v1 (secp256k1 / Keccak-256 / RLP / EIP-1559 + EIP-155 / EIP-55).

See [`docs/hardware-notes.md`](docs/hardware-notes.md) for the Shrike-Fi pin
map and the ESP32-S3 ↔ ForgeFPGA configuration protocol (still relevant for
the [`firmware/bringup/`](firmware/bringup/) app).

## Build status

| Phase | State |
|---|---|
| P0 — board bring-up | 🟢 done — bringup firmware flashes, console works, FPGA accepts bitstreams over SPI |
| **P1 — device root + BIP-39/32 + ETH** | 🟡 **in progress** — pivoting from FPGA-PUF to ESP32-only RoT |
| P2 — ETH signing + calldata decode | ⚪ planned |
| P3 — USB-CDC transport + host client | ⚪ planned |
| P4 — hardening & release | ⚪ planned |
| P5 — PUF revival (post-v1) | ⚪ deferred |

## Toolchains

- **ESP32-S3 firmware:** [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.3.x (`idf.py`). Set up with `~/esp/esp-idf/install.ps1 esp32s3` then `~/esp/esp-idf/export.ps1`.
- **Flashing:** `esptool` (`pip install esptool`) — board enters download mode by holding **BOOT** while plugging in USB-C the first time.
- **Crypto vendor:** [`trezor-crypto`](https://github.com/trezor/trezor-firmware/tree/main/crypto) (MIT) — BIP-32 + BIP-39 + secp256k1 + Keccak — used in production hardware wallets, ESP32-friendly.

## Licence

- **Software** (firmware, host-tools): GPL-2.0 (matches the upstream Shrike ecosystem).
- **HDL** (the deferred `fpga/` tree): GPL-2.0 / CERN-OHL-W (TBD).

## Credits

Built on the [Vicharak Shrike](https://github.com/vicharak-in/shrike) open
hardware ecosystem. The ESP32-S3 ↔ SLG47910 SPI configuration sequence is
derived from Vicharak's MicroPython / Arduino `shrike` libraries (GPL-2.0).
