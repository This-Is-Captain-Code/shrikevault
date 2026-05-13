idf_component_register(
    SRCS
        "main.c"
        "fpga_config.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver                      # SPI master, GPIO
        console                     # esp_console REPL
        esp_driver_usb_serial_jtag  # REPL over the built-in USB Serial/JTAG
        esp_driver_ledc             # LEDC for driving a clock signal to the FPGA
        esp_timer
    PRIV_REQUIRES
        spi_flash                   # esp_flash_get_size() in cmd_info
    # Embed the known-good ForgeFPGA test bitstreams straight into the app image
    # (~46 KB each). One auditable artifact beats a separate filesystem partition.
    # All three are copies of fpga/test/*.bin — see firmware/bringup/README.md.
    EMBED_FILES
        "bitstreams/led_blink.bin"
        "bitstreams/breathing_led.bin"
        "bitstreams/blink_all.bin"
        "bitstreams/pll_oscillator.bin"
        "bitstreams/counter_4bit.bin"
)
