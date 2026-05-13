/*
 * fpga_config.h — drive the Renesas SLG47910 "ForgeFPGA" SRAM configuration
 * port from the ESP32-S3 over SPI2.
 *
 * Configuration handshake (see docs/hardware-notes.md):
 *   1. SS=high, EN=0, PWR=0          hold FPGA powered down
 *   2. wait
 *   3. EN=1, PWR=1                    power up the FPGA core
 *   4. wait
 *   5. SS=high; wait; SS=low          short CS pulse: begin configuration
 *   6. SPI-write the whole bitstream  (MSB-first, SPI mode 0)
 *   7. wait; SS=high                  configuration done; user design runs
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Initialise the GPIOs (PWR/EN/SS) and add the FPGA as an SPI2 device.
 * Call once at startup. Leaves the FPGA powered down. */
esp_err_t fpga_config_init(void);

/* Power down the FPGA (PWR=0, EN=0, SS=high). */
void fpga_power_down(void);

/* Run the full handshake and stream `len` bytes of `bitstream` into the FPGA.
 * `len` should normally be SHRIKE_FPGA_BITSTREAM_BYTES; a mismatch is logged
 * (warning, not fatal — useful while experimenting). */
esp_err_t fpga_load_bitstream(const uint8_t *bitstream, size_t len);

/* Read the 6 MCU<->FPGA interconnect lines (GPIO35..40) as inputs and return
 * them packed in bits 0..5. Handy bring-up sanity check once a design is loaded. */
uint8_t fpga_read_io_lines(void);
