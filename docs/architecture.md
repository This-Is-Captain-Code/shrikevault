# ShrikeVault — architecture

> Companion docs: [`threat-model.md`](threat-model.md) (what this does/doesn't
> protect against — read it), [`hardware-notes.md`](hardware-notes.md) (board
> facts & pin map), [`roadmap.md`](roadmap.md) (the P0→P5 build plan).

## Goals & non-goals

**Goals**
- Ethereum cold-storage signer on the Shrike-Fi (~$9 open hardware).
- Standard, recoverable wallet: a BIP-39 mnemonic is shown **once** at first
  init for paper backup; from then on it's a normal HD wallet.
- Fully open-source, auditable firmware and host code; reproducible builds.
- Sign EIP-1559 (type-2) and legacy (type-0, EIP-155) Ethereum transactions
  over USB-CDC, with on-device display/decoding of what's being signed.

**Non-goals (for v1)**
- Not a true air-gap (USB-CDC is a wire — see threat model). QR + camera is a
  *possible later* milestone, not v1.
- Not multi-coin. Ethereum only.
- **Not a Secure-Element-grade product.** No claim of resistance to a determined
  physical attacker with lab equipment — see threat model.
- No EIP-712 typed-data signing in v1 (later; displaying it safely is real work).
- **No FPGA-PUF in v1.** The original design used a ForgeFPGA RO-PUF as the
  root of trust. We dropped that because the ForgeFPGA toolchain (Renesas
  *ForgeFPGA Workshop*) is GUI-only — no CLI, no scriptable build, no
  `--headless`. That's incompatible with reproducible / hands-off builds.
  The PUF is deferred to a post-v1 enhancement (see [`roadmap.md`](roadmap.md) P5);
  the early scaffolding for it is preserved in [`../fpga/`](../fpga/).

## System overview (v1)

```
                          Shrike-Fi board
  ┌──────────────────────────────────────────────────────────────────────┐
  │  ESP32-S3  (wallet firmware, ESP-IDF)         SLG47910 ForgeFPGA      │
  │                                                                       │
  │  ┌──────────────────────┐                                             │
  │  │ device_key           │  ←─ esp_random() (hardware RNG, first init) │  (FPGA is held in
  │  │ 256-bit root         │      stored in encrypted NVS                │   power-down /
  │  └──────────┬───────────┘                                             │   not used in v1)
  │             ▼ HKDF-SHA256                                             │
  │  ┌──────────────────────┐                                             │
  │  │ bip39_bip32          │  BIP-39 entropy → mnemonic (shown ONCE) →   │
  │  │ secp256k1            │  PBKDF2-SHA512 → BIP-32 master →            │
  │  │ (trezor-crypto)      │  m/44'/60'/0'/0/i                           │
  │  └──────────┬───────────┘                                             │
  │             ▼                                                          │
  │  ┌──────────────────────┐   ┌────────────────┐   ┌─────────────────┐  │
  │  │ eth_tx               │   │ UI: 2 LEDs +   │   │ wallet_transport│◀─┼──▶ host PC
  │  │  RLP, EIP-1559/155,  │   │ BOOT button +  │   │ (TinyUSB CDC-ACM│  │  (ethers /
  │  │  RFC-6979 ECDSA,     │   │ (later: OLED)  │   │  framed proto)  │  │   web3.py +
  │  │  calldata decode     │   └────────────────┘   └─────────────────┘  │   thin client)
  │  └──────────────────────┘                                             │
  └──────────────────────────────────────────────────────────────────────┘
       Secrets (device_key, BIP-32 master, private keys, mnemonic) live in:
         • encrypted NVS (at rest, protected by ESP32 Flash Encryption)
         • RAM (in use, wiped after each operation)
       Mnemonic is shown ONCE on first init then wiped; user keeps the paper.
```

## Root of trust (device_key)

### How it works in v1

**First boot:**
1. Call `esp_random()` (and/or `esp_fill_random`) 32 times to draw 256 bits
   from the ESP32-S3's hardware random number generator. (The ESP32 TRNG seeds
   from RF/Wi-Fi noise + on-die thermal noise. We do a self-check + over-sample
   to mitigate any single-source weakness.)
2. Take the 256 bits as the **device root**.
3. Persist it to the `device` namespace in NVS (encrypted NVS in release builds —
   the encryption key is one of the ESP32's eFuse-burnt keys, unique per chip).
4. Derive **BIP-39 entropy** from `HKDF-SHA256(device_key, info="bip39")`.
5. Convert to a 24-word **mnemonic**; display it on the console exactly once
   and require an explicit confirmation ("yes I wrote it down").
6. Wipe the mnemonic from RAM. Continue with the BIP-32 master derived from
   the standard `PBKDF2-SHA512(mnemonic, "mnemonic", 2048)` seed.

**Every subsequent boot:**
1. Read `device_key` from NVS.
2. Re-derive BIP-39 entropy → seed → BIP-32 master (same values every time;
   mnemonic is *not* shown again).

### What this trust model is (and isn't)

It's a **persistent-encrypted-secret** model — same shape as Trezor and Ledger
(minus the secure element they each carry). The secret lives on-chip, encrypted
with a per-chip key burnt into the ESP32's eFuse at Secure Boot enablement.
Read out the flash chip on a soldering bench and you get only the encrypted
blob; you'd need to defeat the ESP32 itself to recover the key.

What it is NOT, compared to the original PUF design:
- ❌ **Not volatile.** The key persists in NVM (encrypted, but it's there).
  Power-off does not destroy it. The PUF design's "key only exists while
  powered" property is lost.
- ❌ **Not device-bound by silicon variation.** It's bound by the eFuse
  flash-encryption key — different security primitive, weaker against
  invasive attacks that can read eFuse.
- ✅ **Reproducible & auditable.** No closed FPGA toolchain in the trust path;
  the entire wallet is buildable with open `esp-idf` + open trezor-crypto.

See the threat model for the full trade-off.

## Key hierarchy (unchanged from the original plan)

```
device_key (256 bit, from HW RNG once, persisted in encrypted NVS)
   │  HKDF-SHA256(key=device_key, info="bip39")
   ▼
BIP-39 entropy (256 bit)  ──encode──▶  24-word mnemonic   (SHOWN ONCE, then wiped)
   │  PBKDF2(mnemonic, "mnemonic" + optional passphrase, 2048, SHA-512) → 512-bit seed
   ▼
BIP-32 master node  ──CKDpriv──▶  m / 44' / 60' / 0' / 0 / index      (ETH account keys)
   │
   ▼
secp256k1 keypair  ──▶  address = keccak256(uncompressed_pubkey[1:])[12:]  (EIP-55 checksummed)
```

The "user passphrase" hook is left in `bip39_bip32`'s API but disabled by
default (decision locked: v1 = mnemonic-only, no passphrase).

## Ethereum signing path

- **Crypto primitives:** vendored from
  [`trezor-crypto`](https://github.com/trezor/trezor-firmware/tree/main/crypto)
  (MIT) — secp256k1, Keccak-256 (pre-NIST padding, *not* SHA3-256), HMAC-SHA256/512,
  PBKDF2-SHA512, BIP-32, BIP-39. This is the same library running in real Trezor
  devices, reviewed in the wild, and well-suited to ESP32.
  Signing uses **RFC-6979 deterministic nonces** (no RNG in the signature path).
- **Transaction support:**
  - Legacy (type-0) with **EIP-155** chain-id replay protection.
  - **EIP-1559 (type-2)** — `maxFeePerGas` / `maxPriorityFeePerGas` / `accessList`.
  - RLP encode/decode in firmware (small, self-contained, fuzz-tested).
- **On-device display / blind-signing mitigation:** the device parses and
  shows *to, value (ETH), gas params, chainId, nonce*. For `data`:
  - empty → "plain ETH transfer", show recipient + amount;
  - ERC-20 `transfer(address,uint256)` / `approve(address,uint256)` → decoded
    + shown (loud flag on `approve(_, 2^256-1)`);
  - anything else → "**unknown contract interaction, N bytes of calldata** —
    confirm only if you know what this is" + 4-byte selector.
  - EIP-712 typed-data: **not in v1**.
- **Confirmation UX:** v1 has only 2 LEDs + the BOOT button — so the *host*
  renders the human-readable summary, the device shows a fingerprint of
  exactly-what-it-will-sign that the host also shows, and BOOT = confirm.
  (A real on-device display is the obvious post-v1 upgrade and removes the
  trust-the-host gap.)

## Host transport (USB-CDC, v1)

- Device side: **TinyUSB CDC-ACM** (ESP-IDF `esp_tinyusb`). Firmware
  re-enumerates from "USB-Serial-JTAG console" to a CDC device when the
  wallet app starts.
- Wire protocol: simple length-prefixed, CRC'd request/response frames;
  messages are CBOR (or a tiny TLV). Operations: `GET_PUBKEY(path)`,
  `GET_ADDRESS(path)`, `SIGN_ETH_TX(path, rlp_unsigned, chain_id, decoded_hints)`,
  `SIGN_MESSAGE(path, msg)` (personal_sign / EIP-191), `WIPE`, `DEVICE_INFO`.
- Host side: a thin Python client + an example integration (`web3.py` /
  `ethers` external-signer shim) in [`../host-tools/`](../host-tools/).
- **Not air-gapped:** see threat model. The protocol is designed so a QR/SD
  transport can be slotted under it later without touching the signing core.

## Repo layout

```
firmware/
  bringup/            P0 board bring-up app (done; useful for HW debugging)
  shrikevault/        the wallet itself  (P1+)
  components/
    device_key/       first-init root via HW RNG → encrypted NVS
    bip39_bip32/      BIP-39 mnemonic + BIP-32 derivation (trezor-crypto wrap)
    eth_tx/           RLP, EIP-1559/155, EIP-55, RFC-6979 signing, calldata decode
    wallet_transport/ TinyUSB CDC-ACM framing + request/response dispatch
fpga/                 (deferred) PUF scaffolding for the post-v1 revival
host-tools/           probe_board.py; later: shrikevault_cli.py + web3 shim
docs/                 (this directory)
```
