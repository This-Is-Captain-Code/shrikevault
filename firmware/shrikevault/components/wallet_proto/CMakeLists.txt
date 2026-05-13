idf_component_register(
    SRCS "wallet_proto.c"
    INCLUDE_DIRS "include"
    REQUIRES device_key bip39_bip32 eth_tx
    PRIV_REQUIRES esp_hw_support esp_app_format
)
