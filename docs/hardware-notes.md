# Hardware notes — Shrike-Fi (ESP32-S3 + Renesas SLG47910 ForgeFPGA)

Distilled from the [Vicharak `shrike` repo](https://github.com/vicharak-in/shrike)
docs/examples and the Renesas SLG47910 datasheet. **Verify against your actual
board revision before trusting any pin number** — Vicharak's docs cover several
revisions and the Shrike-Fi was the newest at time of writing.

## Board summary

| | |
|---|---|
| MCU | Espressif **ESP32-S3** (Xtensa LX7 dual-core, USB-OTG) |
| FPGA | Renesas **SLG47910V** "ForgeFPGA": 1120 × 5-input LUTs, 1120 FFs, ~5 kb distributed RAM, 32 kb block RAM (2 × 16 kb), on-chip 50 MHz osc + PLL, 19 GPIO, OTP **or** SPI configuration |
| MCU flash | 8 MB QSPI |
| PSRAM | **Not populated** on the base board (optional ≤16 MB add-on — solder-it-yourself) |
| Other | 2 user LEDs, BOOT button, USB-C (power + programming), optional BMS / battery (add-on), WiFi + BLE (ESP32-S3) |
| FPGA config | **SRAM-loaded**: the ESP32-S3 streams a ~46408-byte bitstream into the SLG47910 over SPI on every power-up (the FPGA does not retain config across power loss in this mode) |

The same SLG47910 + same bitstream is shared across the whole Shrike family
(Shrike-Lite/RP2040, Shrike/RP2350, Shrike-Fi/ESP32-S3) — only the host MCU and
its pin map differ.

## ESP32-S3 ↔ ForgeFPGA pin map (Shrike-Fi)

| Function | ESP32-S3 GPIO | Notes |
|---|---|---|
| `FPGA_PWR` | **GPIO 4** | FPGA power-domain enable (drive low to power-cycle the FPGA) |
| `FPGA_EN` | **GPIO 5** | FPGA enable |
| `FPGA_SPI_SS` | **GPIO 10** | SPI chip-select to the FPGA config port (active low during streaming) |
| `FPGA_SPI_SCK` | **GPIO 12** | SPI clock — mode 0 (CPOL=0, CPHA=0), MSB-first, ≤16 MHz (examples use 1.6–8 MHz) |
| `FPGA_SPI_MOSI` | **GPIO 11** | bitstream data MCU→FPGA |
| `FPGA_SPI_MISO` | **GPIO 13** | unused for config (write-only stream); available for user designs |
| `UART_TX` (MCU→FPGA) | **GPIO 17** | general UART link to FPGA fabric |
| `UART_RX` (FPGA→MCU) | **GPIO 18** | |
| FPGA↔MCU interconnect | **GPIO 35, 36, 37, 38, 39, 40** | 6 general-purpose IO lines between ESP32-S3 and FPGA `GPIO0..GPIO5` (FPGA pins 13..18). This is where the PUF challenge/response interface will live. |

> **Use SPI2 (the "HSPI"/FSPI peripheral), not the bus tied to internal flash.**
> Vicharak hit a real FreeRTOS-vs-flash-mutex crash using the default bus on
> ESP32; their Arduino lib forces a dedicated `SPIClass(HSPI)`. In ESP-IDF terms:
> use `SPI2_HOST` with the GPIO matrix routing the pins above.

(For reference — *not* Shrike-Fi — the RP2040/RP2350 boards use SPI0 with
SS=1, SCK=2, MOSI=3, MISO=0, EN=13, PWR=12.)

## ForgeFPGA configuration sequence (the "flash the bitstream" handshake)

Reconstructed from `archive/MCU_FFPGA_script/MCU_FFPGA_uploading_bitstream.py`,
`archive/shrike_micropy/shrike_fpga.py`, `utils/shrike-ctl/main.c`, and the
platform-agnostic firmware guide in the upstream repo:

```
1.  SS = high (idle), EN = 0, PWR = 0          # hold FPGA in power-down
2.  wait ~3–100 ms
3.  EN = 1, PWR = 1                              # power up the FPGA core
4.  wait ~3–100 ms
5.  SS = high, wait ~2–3 ms, SS = low           # short CS pulse: begin config
6.  SPI write the entire bitstream (46408 bytes), MSB-first, SPI mode 0
7.  wait ~100 ms
8.  SS = high                                   # config complete; user design runs
```

To **reset / power down** the FPGA: `PWR = 0` (and re-run the sequence to
reconfigure). The exact `EN` vs `PWR` timing isn't critical — Vicharak's two
scripts use 100 ms; `shrike-ctl` uses 3 ms — but `PWR`/`EN` must be high and
stable *before* the CS pulse, and the full bitstream must be streamed in one
continuous SPI transaction.

> **Bitstream size:** the SLG47910 config is a fixed ~**46408 bytes** for this
> part. `fpga/test/led_blink.bin` and `fpga/test/blink_all.bin` (copied from the
> upstream repo's `test/bitstreams/v1_4/`) are exactly that size and are useful
> bring-up payloads.

## Where the bitstream lives

Upstream stores FPGA bitstreams in the MCU's flash:
- **Arduino path:** LittleFS partition, file uploaded with the `arduino-littlefs-upload` tool, read back by the `Shrike`/`ShrikeFlash` library.
- **MicroPython path:** plain file on the MicroPython filesystem, `shrike.flash("name.bin")`.

For ShrikeVault (ESP-IDF) we'll keep it simple: **embed the bitstream(s) in the
firmware image** via `EMBED_FILES` in CMake (it's only ~46 KB and we want a
single auditable, reproducibly-built artifact). A dedicated read-only flash
partition is the fallback if we ever need field-updatable bitstreams.

## Flashing the ESP32-S3

Hold **BOOT** while connecting USB-C to enter the ROM download mode, then:

```powershell
python -m esptool --chip esp32s3 erase_flash
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset `
    write_flash -z 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 app.bin
# (idf.py flash does the offsets for you)
```

The board's native USB enumerates as **USB-Serial-JTAG** (`VID 303A` / `PID 1001`)
— that's the ESP32-S3's built-in console/JTAG, used for `idf.py monitor` and
`idf.py flash`. When our firmware brings up TinyUSB-CDC for the wallet protocol,
it will re-enumerate as a TinyUSB CDC-ACM device instead (expected).

## Renesas SLG47910 (ForgeFPGA) — resources relevant to the PUF

- **1120 5-input LUTs + 1120 FFs** — plenty for a ring-oscillator PUF bank
  (a single inverter-chain RO is a handful of LUTs; ~64–128 ROs + counters +
  challenge mux is well within budget) plus a small SPI/parallel slave to the MCU.
- **No user control over place-and-route** (closed vendor tool) → we cannot pin
  RO placement for routing symmetry. RO-pair uniqueness/reliability **must be
  characterised on real silicon** (see `docs/roadmap.md` P1), not assumed.
- **Block RAM is initialised by the config process** → an SRAM-power-up PUF is
  **not** practical on this part. RO-PUF (or TERO/loop PUF) is the realistic choice.
- **50 MHz on-chip oscillator + PLL** available as a reference clock for RO
  frequency measurement (count RO edges over a fixed number of reference cycles).

## Open questions / to verify on hardware

- [ ] Exact `FPGA_PWR`/`FPGA_EN` semantics and minimum timing on the Shrike-Fi revision in hand.
- [ ] Which of GPIO 35–40 are input-only on ESP32-S3? (GPIO 26–32 are SPI-flash on -S3; 33–37 may be octal-PSRAM on modules with PSRAM — but base Shrike-Fi has no PSRAM. Confirm none of 35–40 collide with anything on *this* board.)
- [ ] Does the SLG47910 config port give any "config done" / readback signal we can sense (MISO or a GPIO), or is it open-loop?
- [ ] BOOT button GPIO (likely GPIO0) and the two user-LED GPIOs on Shrike-Fi.
- [ ] Whether the board exposes the FPGA's own GPIO well enough to also use them as PUF challenge lines, or if the 6 MCU-interconnect lines are the only practical channel.
