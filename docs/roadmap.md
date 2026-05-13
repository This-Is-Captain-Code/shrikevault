# ShrikeVault — roadmap

Decisions locked: **Ethereum only** for v1; **BIP-39 mnemonic shown once** as
the recovery model; **USB-CDC** as the v1 host transport; **ESP32-only**
root-of-trust (FPGA-PUF deferred to post-v1 — see P5 below).
See [`architecture.md`](architecture.md) and
[`threat-model.md`](threat-model.md).

---

## P0 — Board bring-up *(done)*

Proved the ESP32 ↔ ForgeFPGA link works end-to-end:
firmware runs, FPGA gets powered/enabled, SPI streaming + handshake work, and
the FPGA's output state is a function of what we send over SPI (load 6
different bitstreams, get 6 different output states).

What we *learned* along the way and want to remember:
- Two wrong pin numbers in the upstream platform-agnostic guide vs. the
  canonical `shrike_pinouts.md`: **PWR=GPIO8, EN=GPIO9** (not 4/5);
  **MCU user LED=GPIO21** (not 2). Pin fix is in
  [`firmware/bringup/main/shrike_board.h`](../firmware/bringup/main/shrike_board.h).
- The Shrike-Fi only wires **4 FPGA pins** to the ESP32 (the SPI bus), unlike
  Shrike-Lite which has 6. None of the upstream example bitstreams have been
  tested on Shrike-Fi (the `pll_oscillator` README explicitly notes
  "Shrike-fi: Untested"), and they all expect an external clock on FPGA
  pins 17/18 that isn't wired here — so none of them blink the user LED.
- **ForgeFPGA Workshop is GUI-only**: no CLI, no scriptable build, no
  `--headless` flag. Yosys is invokable from the shell but the proprietary
  PnR / bitstream-packing lives inside `coregp.dll` and only runs from the
  GUI. This is what drove the pivot below.

Deliverables landed:
[`firmware/bringup/`](../firmware/bringup/) (ESP-IDF, builds + flashes + boots
clean); `idf.py` set up; ESP-IDF v5.3.2 installed; full docs.

---

## P1 — Device root + BIP-39/32 + ETH address  *(in progress)*

**Goal:** the device has a unique persistent secret it can derive ETH keys from.

Pivot summary: the original plan derived the root from a ForgeFPGA RO-PUF +
fuzzy extractor (volatile, reconstructed each boot). With the FPGA tooling
unusable from the command line, v1 derives the root **once** from the
ESP32-S3's **hardware RNG** at first init and persists it in encrypted NVS.
Security baseline: ESP32-S3 **Secure Boot v2** + **Flash Encryption** + **NVS
encryption** + **anti-rollback** (enabled in release builds in P4). This is
roughly Trezor-class minus the secure-element — see threat model for the
honest trade-offs.

- [ ] `firmware/shrikevault/` ESP-IDF app skeleton — separate from `bringup/`.
- [ ] `components/device_key/` — first-init root generation via
      `esp_random()` (drives the on-chip hardware RNG), stored in NVS,
      readable for downstream derivations. Stub for "encrypted NVS" enabled
      in release builds.
- [ ] Vendor [`trezor-crypto`](https://github.com/trezor/trezor-firmware/tree/main/crypto)
      (MIT) — gives us BIP-32, BIP-39, secp256k1, Keccak in one well-reviewed
      C library used by real hardware wallets.
- [ ] `components/bip39_bip32/` — wraps trezor-crypto: device root → BIP-39
      entropy → mnemonic (shown **once** on first init, wiped from RAM after) →
      PBKDF2-SHA512 seed → BIP-32 master → `m/44'/60'/0'/0/i`.
- [ ] First useful console command on the device: `get_address [index]` —
      derives and prints the EIP-55 checksummed ETH address.
- [ ] **Done when:** same board → same mnemonic & first address on every boot;
      mnemonic is only shown when explicitly requested at first init (and you
      have to type a "I wrote it down" confirmation before it's wiped); ETH
      address matches what a reference wallet (`ethers`/`web3.py`) produces
      for the same mnemonic.

---

## P2 — ETH signing core + calldata decode

**Goal:** correct ETH signatures with on-device transaction display.

- [ ] `components/eth_tx/`: RLP encode/decode, **legacy (EIP-155)** + **EIP-1559 (type-2)** tx building, **RFC-6979 deterministic ECDSA**, EIP-55 address checksums, `personal_sign` / EIP-191.
- [ ] Calldata decoder: empty → ETH transfer; ERC-20 `transfer`/`approve` → decoded (loud flag on `approve(_, 2^256-1)`); anything else → "unknown contract interaction, N bytes" + 4-byte selector.
- [ ] Unit tests against published BIP-32 / EIP-155 / EIP-1559 / EIP-55 test vectors (run on host + on-device).
- [ ] **Done when:** signatures verify against `ethers`/`web3.py`; same addresses as standard wallets for the same mnemonic.

---

## P3 — USB-CDC transport + host client

- [ ] `components/wallet_transport/`: TinyUSB CDC-ACM (`esp_tinyusb`), length-prefixed CRC'd frames, CBOR/TLV messages, dispatch: `DEVICE_INFO`, `GET_PUBKEY(path)`, `GET_ADDRESS(path)`, `SIGN_ETH_TX(path, rlp_unsigned, chain_id, hints)`, `SIGN_MESSAGE(path, msg)`, `WIPE`.
- [ ] Consent gate: the device shows a fingerprint of *exactly what it will sign*; the host shows the same fingerprint + the human-readable decoded summary; **BOOT-button press = confirm**.
- [ ] `host-tools/shrikevault_cli.py` + a `web3.py` external-signer shim + a README walking through "build an unsigned tx → confirm on device → broadcast".
- [ ] **Done when:** an end-to-end testnet (Sepolia/Holesky) transfer is signed on-device and broadcast from the host.

---

## P4 — Hardening + release

- [ ] Release `sdkconfig`: **Secure Boot v2**, **Flash Encryption**, encrypted NVS, **anti-rollback**, JTAG disabled in release, USB-Serial-JTAG console gated; debug builds clearly stamped "INSECURE".
- [ ] Reproducible builds (pinned ESP-IDF + toolchain hashes, published binary ⇄ source verification).
- [ ] Constant-time / side-channel review of the signing path; sanity-checks on the BOOT-consent gate.
- [ ] Final threat-model doc + security.md / disclosure policy; tag `v0.1.0-preview`; invite review. **Still "do not use with real funds" until audited.**

---

## P5 — Post-v1 enhancements

- **PUF revival.** Restore the FPGA-PUF as a stronger root-of-trust, gated on
  either: (a) UI-automation of ForgeFPGA Workshop that's reliable enough to
  drop into CI, or (b) the appearance of an open-source toolchain for the
  SLG47910. Scaffolding is preserved at [`../fpga/`](../fpga/) (HDL, sim,
  BUILD.md). With a PUF, the key disappears on power-off, helper data is the
  only thing in NVM, and the device becomes un-cloneable — meaningful
  security upgrades over the v1 baseline.
- **On-device display** (Qwiic/SPI OLED) + **camera** → real **QR air-gapped** flow.
- **EIP-712** typed-data signing (displayed safely).
- **WalletConnect** / better dapp integration on the host side.
- **Multi-coin** (BTC + PSBT, etc.) — the secp256k1/BIP-32 plumbing already exists.

---

## Status board

| Phase | State |
|---|---|
| P0 — bring-up | 🟢 done |
| P1 — device root + BIP-39/32 + ETH address | 🟡 in progress |
| P2 — ETH signing + calldata decode | ⚪ not started |
| P3 — USB-CDC + host client | ⚪ not started |
| P4 — hardening + release | ⚪ not started |
| P5 — PUF revival / display / extras | ⚪ deferred |
