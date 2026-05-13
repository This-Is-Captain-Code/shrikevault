# trezor_crypto — vendored from github.com/trezor/trezor-firmware/crypto/  (MIT)
#
# We compile only the subset we need for an Ethereum wallet:
#   BIP-32 + BIP-39 + secp256k1 (ECDSA) + Keccak (sha3) + SHA-2 + HMAC + PBKDF2
#   + RIPEMD160 + base58 (BIP-32 uses them for fingerprints / extended keys).
# Everything else stays vendored as files but is *not compiled* — those .c
# files (cardano, nem, ed25519, monero, blake, etc.) are dead code from this
# project's perspective. The corresponding *headers* are kept so that the
# files we DO compile (e.g. bip32.c which has unconditional #include "cardano.h")
# build cleanly with feature flags off.
#
# Compile-time feature flags (see crypto/options.h and bip32.c's #ifdefs):
#   USE_ETHEREUM=1          → enables hdnode_get_ethereum_pubkeyhash()
#   USE_CARDANO=0           → off
#   USE_KECCAK=0            → off (the Keccak ed25519 variant — we still get
#                              raw Keccak-256 via sha3.c which is unconditional)
#   USE_NEM=0               → off
#   USE_BIP32_CACHE=0       → off
#   USE_BIP32_25519_CURVES=0→ off (no Ed25519 BIP-32)

idf_component_register(
    SRCS
        # Hash primitives
        "sha2.c"
        "sha3.c"
        "ripemd160.c"
        "hmac.c"
        "pbkdf2.c"
        "hasher.c"           # generic hash-dispatch shim used by BIP-32

        # Big-number + EC math
        "bignum.c"
        "ecdsa.c"
        "secp256k1.c"
        "nist256p1.c"        # bip32.c's get_curve_by_name() references &nist256p1_info — link-time only

        # Encodings
        "base58.c"
        "memzero.c"

        # BIP-32 / BIP-39
        "bip32.c"
        "bip39.c"
        "bip39_english.c"    # English wordlist

        # Curve registry (lookup by name like "secp256k1")
        "curves.c"

        # RNG: random_uniform/random_permute helpers + our ESP-IDF random_buffer
        "rand.c"
        "shrike_random.c"

    INCLUDE_DIRS "."
    REQUIRES esp_system
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    USE_ETHEREUM=1
    USE_KECCAK=1               # gates plain Keccak-256 (used for ETH addresses)
    USE_CARDANO=0
    USE_NEM=0
    USE_BIP32_CACHE=0
    USE_BIP32_25519_CURVES=0
)

# trezor-crypto's coding style triggers a handful of pedantic warnings that are
# benign here; silence them so the build is clean rather than wall-of-warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-shift-negative-value
    -Wno-array-bounds
    -Wno-missing-field-initializers
)
