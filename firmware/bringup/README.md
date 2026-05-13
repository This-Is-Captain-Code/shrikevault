# `bringup` — P0 board bring-up app (ESP-IDF)

Smallest useful firmware: prove we own the Shrike-Fi end to end.

What it does on boot:
1. Blinks the ESP32-S3 user LED (heartbeat — "firmware is alive").
2. Initialises SPI2 + the FPGA control GPIOs, power-cycles the SLG47910, and
   streams the embedded ForgeFPGA test bitstream into it — so the **FPGA's own
   LED** should start blinking too (the embedded `bitstreams/led_blink.bin` is
   Vicharak's `led_blink` design, a copy of `../../fpga/test/led_blink.bin`).
3. Starts a console over the ESP32-S3 **built-in USB Serial/JTAG** (the port that
   already shows up as a COM device — no extra UART adapter).

Console commands: `help`, `info`, `fpga_load`, `fpga_off`, `io_read`, `puf` (stub).

> This is a **DEBUG build** — Secure Boot and Flash Encryption are intentionally
> *off* here. They land in the release `sdkconfig` in P5. Don't use this build
> for anything but bring-up.

## Build & flash

Requires ESP-IDF v5.3.x (installed at `~/esp/esp-idf` on this machine).

```powershell
# once per shell:
& "$env:USERPROFILE\esp\esp-idf\export.ps1"

# from the repo root:
idf.py -C firmware/bringup set-target esp32s3
idf.py -C firmware/bringup build

# put the board in download mode: hold BOOT while plugging in USB-C (first time only)
idf.py -C firmware/bringup -p COM19 flash monitor
```

(`idf.py monitor` talks to the same USB Serial/JTAG console; press the prompt's
Enter to get the `shrikevault>` prompt. Ctrl-] to exit the monitor.)

## Files

| File | Purpose |
|---|---|
| `main/main.c` | app entry, LED heartbeat, auto-load bitstream, console + commands |
| `main/shrike_board.h` | Shrike-Fi pin map (⚠ a couple of pins are placeholders — see the header) |
| `main/fpga_config.{h,c}` | SLG47910 SRAM-config driver: power/EN/SS sequencing + SPI streaming |
| `main/bitstreams/led_blink.bin` | embedded test bitstream (copy of `fpga/test/led_blink.bin`) |
| `sdkconfig.defaults` | target = esp32s3, 8 MB flash, USB-Serial-JTAG console, PSRAM off |
| `CMakeLists.txt`, `main/CMakeLists.txt` | ESP-IDF project/component definitions |

## "Done" criteria for P0

- `idf.py build` succeeds with this exact ESP-IDF version.
- After flashing: ESP32 LED blinks **and** the FPGA LED blinks.
- `info` and `fpga_load` work on the console; `fpga_off` stops the FPGA LED;
  re-running `fpga_load` brings it back.
- Anything that differed from `docs/hardware-notes.md` (pin numbers, timings,
  PSRAM, LED/BOOT GPIOs) gets recorded back into that file.
