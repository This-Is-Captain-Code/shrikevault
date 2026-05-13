idf_component_register(
    SRCS "eth_tx.c"
    INCLUDE_DIRS "include"
    REQUIRES bip39_bip32 trezor_crypto
)
