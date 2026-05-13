idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        console                     # esp_console REPL
        esp_driver_usb_serial_jtag  # REPL over the built-in USB Serial/JTAG
        device_key                  # our v1 root-of-trust component
        bip39_bip32                 # BIP-39 mnemonic + BIP-32/ETH address derivation
)
