# ShrikeVault — threat model (read this before trusting anything)

> **Bottom line:** ShrikeVault is a hobbyist / research open-hardware wallet. It
> raises the bar meaningfully over "private key in a hot wallet on your laptop"
> and is roughly equivalent to a **Trezor minus the secure element** —
> a persistent encrypted seed protected by the ESP32-S3's flash encryption +
> secure boot. It is **not** a Secure-Element product and makes **no claim** of
> resisting a determined attacker with physical access and lab equipment.
> Don't put more on it than you'd be comfortable losing to a firmware bug or
> a side-channel attack.

> **What changed from the original design:** v1 derives its root-of-trust from
> the ESP32-S3's hardware RNG (one-shot at first init), stored in encrypted NVS.
> The original ForgeFPGA RO-PUF + fuzzy-extractor design — where the key was
> reconstructed each boot and never lived in NVM — is deferred. See
> [`architecture.md`](architecture.md) for the why and
> [`roadmap.md`](roadmap.md) P5 for the path back.

## Assets

1. The **master seed** / BIP-39 entropy / BIP-32 master key (controls all funds).
2. Per-account **private keys** derived from it.
3. The **BIP-39 mnemonic** shown once at init (the user's paper backup — its
   security is now the user's physical-security problem).
4. **Integrity** of what gets signed (a correct signature over the *intended*
   transaction, not an attacker-substituted one).

Public, *not* assets to keep secret (but integrity-critical): extended
**public** keys, all firmware source, all host code.

## What the v1 trust model gives you (and what it doesn't)

**Properties it does give:**
- The master secret is **never in plaintext on flash**. It's stored in NVS
  encrypted with the ESP32-S3's **per-chip flash-encryption key** (a 256-bit
  AES key burnt into the chip's eFuse at Secure Boot enablement, unique to
  each chip and unreadable outside the chip). A flash-chip read with a SOIC
  clip yields ciphertext + public NVS metadata, not the key.
- **Secure Boot v2** ensures only firmware signed with the project's key can
  run — the chip refuses to boot a tampered or substituted firmware.
- **Anti-rollback** prevents an attacker from re-flashing an older, vulnerable
  firmware version even if it's signed.
- **RFC-6979 deterministic ECDSA** means the signing path has no RNG —
  bad-RNG signature leakage is structurally impossible.
- The seed is **bound to this chip** (encrypted with eFuse-derived key) — the
  ciphertext doesn't decrypt on another ESP32-S3, so cloning is blocked
  short of extracting the eFuse key (which requires invasive attacks on the
  silicon, see "out of scope" below).

**What it explicitly does NOT give:**
- **Not isolation.** All crypto and the seed transit ESP32-S3 RAM during use.
  An attacker who can run code on the chip, glitch it, or read its RAM /
  registers *while it's powered and unlocked* can get the key. This is the
  same exposure as any MCU-based wallet (Trezor One, Keepkey, …).
- **No Secure Element.** No tamper mesh, no hardened key store, no certified
  side-channel countermeasures. ESP32-S3 Flash Encryption + Secure Boot v2 are
  good, but not equivalent to an SE.
- **Not volatile** (unlike the original PUF design). The seed persists in
  encrypted flash. Power-off does not destroy it. A determined attacker who
  defeats Flash Encryption gets the key.
- **Trust in the ESP32-S3 implementation.** Flash Encryption and Secure Boot v2
  are well-reviewed but have had documented attacks in the past (e.g.,
  fault-injection bypass research). We use them at their strongest configuration
  in release builds, but we are trusting Espressif's silicon to behave as advertised.

## In scope (we try to defend against these)

| Threat | Mitigation in design |
|---|---|
| Stolen/seized **powered-off** board, attacker reads flash | Flash Encryption: flash dump yields ciphertext only. Without the per-chip eFuse key the seed isn't recoverable from a flash dump alone. |
| Cloning the board (copy flash to a different ESP32-S3) | Different chip → different eFuse encryption key → the cloned flash won't decrypt. |
| Malicious **host PC** sends a different transaction than the user thinks | Device decodes & shows to/value/gas/chainId/nonce + ERC-20 transfer/approve; shows a fingerprint of exactly-what-it-signs that the host must also display; BOOT = explicit confirm. (Weak in v1 because no on-device screen — see "Known weaknesses".) |
| **Blind-signing** opaque calldata | Device refuses to present it as anything benign — explicit "unknown contract interaction, N bytes" warning + selector shown. |
| Replay across chains | EIP-155 chain-id enforced; chainId shown on the confirm screen. |
| Nonce-reuse / bad-RNG signature leakage | RFC-6979 deterministic ECDSA — no RNG in the signing path. |
| Tampered **firmware** | ESP32-S3 **Secure Boot v2** (signed app), **Flash Encryption**, **anti-rollback**. Reproducible builds so users can verify the published binary matches source. |
| Casual on-path USB sniffing of the protocol | No secrets cross USB except *public* keys/addresses/signatures; signing requires physical BOOT press. |
| Lost board | Recover from the BIP-39 mnemonic written down at init. |

## Out of scope (we do NOT claim to defend against these)

- **Invasive / semi-invasive physical attacks** on a *powered, unlocked* device:
  microprobing, FIB, decapping, EM/laser fault injection, power/EM side-channel
  key extraction during signing. The ESP32-S3 is not hardened for this.
- **eFuse extraction** from a powered or unpowered chip (decap + microprobing
  the eFuse). If an attacker can read the flash-encryption key from the chip,
  Flash Encryption is defeated.
- **Voltage/clock glitching** the ESP32-S3 to skip the BOOT-confirm check, to
  bypass Secure Boot, or to leak the flash-encryption key. We add sanity
  checks, but no real glitch hardening.
- **Supply-chain compromise** of the board, the ESP-IDF / toolchain, or
  vendored crypto (trezor-crypto). We pin versions and aim for reproducible
  builds — but a backdoored toolchain or a tampered board defeats us.
- **A compromised host PC that you also let display the confirmation summary** —
  in v1 there's no trustworthy on-device screen, so a host that lies about
  *both* the summary *and* the fingerprint UI can fool a user who doesn't
  independently verify the recipient/amount. (An on-device display in P5+
  closes this; until then, treat the host as semi-trusted and double-check
  addresses by other means.)
- **The user's paper backup** — if someone photographs/steals the 24 words,
  game over. That's outside the device entirely.
- **RF / WiFi / BLE attack surface** — v1 firmware does **not** bring up WiFi
  or BLE at all; the radios stay off. (If a future variant uses them, that's
  a whole new threat-model section.)
- **Rubber-hose / coercion**, shoulder-surfing the mnemonic at init, etc.

## Security mechanisms we rely on (and their assumptions)

- **ESP32-S3 Secure Boot v2 + Flash Encryption + anti-rollback** — assumes the
  eFuse-based root of trust and AES flash-encryption peripheral are sound and
  correctly enabled. We enable them in `sdkconfig` for release builds; debug
  builds are clearly marked insecure.
- **RFC-6979 deterministic ECDSA** + the `trezor-crypto` library — assumes
  the implementation is correct and constant-time-ish. We pin the version we
  vendor and review its code paths used by the wallet.
- **HKDF / PBKDF2 / HMAC** over SHA-256/512 — standard, assumed sound.
- **Hardware RNG (`esp_random()` / `esp_fill_random`)** at first init — assumes
  the ESP32-S3 TRNG produces ≥256 bits of min-entropy (it sources from RF and
  on-die noise; we draw extra and stir to harden against any single-source
  weakness).
- **Physical BOOT button as the consent gate** — assumes the attacker can't
  press it remotely (true) and can't glitch the firmware into skipping the
  check (mostly out of scope — best-effort sanity checks only).

## Things a v1 user MUST understand

1. **This is pre-alpha and unaudited. Don't use real funds yet.**
2. **Write down the mnemonic at init and protect it like cash.** It is the only recovery path.
3. **It is not air-gapped in v1** — it's on a USB wire to your PC. Treat the PC as
   semi-trusted; verify recipient addresses out-of-band for large transfers.
4. **It is not Secure-Element-grade.** If your threat model includes well-resourced
   physical attackers, this is the wrong device.
5. **A dead/destroyed board ⇒ you must restore from the mnemonic** on a new
   board (or any BIP-39 / BIP-44 wallet). The seed is encrypted *to this chip's
   eFuse key* and cannot be migrated by copying the flash.
