/*
 * shrike_board.h — Shrike-Fi (ESP32-S3 + Renesas SLG47910 ForgeFPGA) pin map.
 *
 * Source: Vicharak `shrike` repo — docs/shrike_pinouts.md ("Shrike-fi" tab) and
 * docs/hardware_overview.md. NOTE: the repo's PLATFORM_AGNOSTIC_FIRMWARE_GUIDE.md
 * lists *different* (stale/draft) numbers for FPGA EN/PWR (5/4) — the pinout doc's
 * 9/8 is the one corroborated by the hardware-overview doc and is used here. The
 * 4 SPI pins agree across both docs. See docs/hardware-notes.md.
 *
 * If FPGA configuration doesn't work, the most likely culprit is still EN/PWR —
 * try the alternate (EN=GPIO5, PWR=GPIO4) before suspecting anything else.
 */
#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

/* ---- ESP32-S3 <-> ForgeFPGA configuration / IO interface ----
 * These 4 SPI pins are dual-purpose: bitstream-config while loading, then a
 * general 4-bit IO bus to talk to the running FPGA design (this is where the
 * PUF challenge/response slave will live in P1). */
#define SHRIKE_PIN_FPGA_PWR     GPIO_NUM_8    /* FPGA PWR (power-domain control)   */
#define SHRIKE_PIN_FPGA_EN      GPIO_NUM_9    /* FPGA EN (enable)                  */
#define SHRIKE_PIN_FPGA_SS      GPIO_NUM_10   /* FPGA pin 4: SPI_SS                */
#define SHRIKE_PIN_FPGA_SCK     GPIO_NUM_12   /* FPGA pin 3: SPI_SCLK             */
#define SHRIKE_PIN_FPGA_MOSI    GPIO_NUM_11   /* FPGA pin 5: SPI_SI  (MOSI)       */
#define SHRIKE_PIN_FPGA_MISO    GPIO_NUM_13   /* FPGA pin 6: SPI_SO  (MISO/CONFIG)*/

/* Use SPI2 ("FSPI"/"HSPI") — NOT the host bound to internal flash. */
#define SHRIKE_FPGA_SPI_HOST    SPI2_HOST

/* SLG47910 SRAM-config bitstream is a fixed size for this part. */
#define SHRIKE_FPGA_BITSTREAM_BYTES   46408

/* Conservative SPI clock for config; the part is good for up to ~16 MHz. */
#define SHRIKE_FPGA_SPI_HZ      (4 * 1000 * 1000)

/* The 4 dual-purpose SPI/IO-bus lines, in a fixed order for `io_read`. */
#define SHRIKE_FPGA_IO_COUNT    4
static const gpio_num_t SHRIKE_PIN_FPGA_IO[SHRIKE_FPGA_IO_COUNT] = {
    SHRIKE_PIN_FPGA_SS, SHRIKE_PIN_FPGA_MOSI, SHRIKE_PIN_FPGA_SCK, SHRIKE_PIN_FPGA_MISO,
};
static const char *const SHRIKE_FPGA_IO_NAME[SHRIKE_FPGA_IO_COUNT] = {
    "SS(GPIO10)", "MOSI(GPIO11)", "SCK(GPIO12)", "MISO(GPIO13)",
};

/* ---- On-board user I/O ---- */
#define SHRIKE_PIN_LED          GPIO_NUM_21   /* ESP32-S3 user LED (active high)   */
#define SHRIKE_PIN_BOOT_BTN     GPIO_NUM_0    /* BOOT button (active low) — std ESP32-S3, unconfirmed for Shrike-Fi */

/* FPGA-side: the FPGA's own user LED is on FPGA pin GPIO16 (active high). The
 * embedded led_blink.bin maps its toggling output there. */
