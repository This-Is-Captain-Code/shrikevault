# `firmware/` — ESP32-S3 firmware (ESP-IDF)

| Dir | Status | What |
|---|---|---|
| [`bringup/`](bringup/) | 🟡 P0, in progress | board bring-up: USB console + FPGA config + link check |
| `shrikevault/` | ⚪ planned (P2+) | the wallet itself — PUF unlock → BIP-39/32 → ETH signing → USB-CDC protocol |
| `components/` | ⚪ planned | shared components: `fpga_config`, `puf` (driver + fuzzy extractor), `bip39_bip32`, `eth_tx`, `wallet_transport` |

The bring-up app currently carries its own copy of the FPGA-config driver under
`bringup/main/`. Once it's validated on hardware it gets promoted to
`components/fpga_config/` and the wallet app reuses it.

## Toolchain

ESP-IDF v5.3.x. On this machine it's installed at `~/esp/esp-idf`:

```powershell
& "$env:USERPROFILE\esp\esp-idf\export.ps1"   # once per shell
idf.py -C firmware/bringup build
```

If you need to (re)install ESP-IDF: `git clone -b v5.3.2 --recurse-submodules
https://github.com/espressif/esp-idf.git ~/esp/esp-idf` then
`~/esp/esp-idf/install.ps1 esp32s3`.

## Build profiles (planned)

- **debug** — what `bringup/` is now: no Secure Boot / Flash Encryption, console
  open, JTAG on. For development only.
- **release** (P5) — Secure Boot v2 (signed app), Flash Encryption, NVS
  encryption, anti-rollback, JTAG disabled, console gated. Reproducible build
  with pinned ESP-IDF + toolchain hashes. This is the only profile that should
  ever touch real keys.
