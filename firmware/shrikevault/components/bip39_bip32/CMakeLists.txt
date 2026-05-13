idf_component_register(
    SRCS "wallet_keys.c"
    INCLUDE_DIRS "include"
    REQUIRES device_key trezor_crypto mbedtls
)
