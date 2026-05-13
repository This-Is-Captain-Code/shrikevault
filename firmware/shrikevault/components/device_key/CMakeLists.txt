idf_component_register(
    SRCS "device_key.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_system
    PRIV_REQUIRES mbedtls
)
