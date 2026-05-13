# `fpga/test/` — known-good test bitstreams

Both files are SLG47910 ForgeFPGA bitstreams (exactly **46408 bytes** — the
fixed config size for this part), from the upstream Vicharak `shrike` repo
(GPL-2.0). Useful payloads for bringing up the ESP32-S3 → FPGA configuration
path before we have our own HDL.

| File | Design | Source | Use |
|---|---|---|---|
| `led_blink.bin` | counter → blink one FPGA-connected LED at ~1 Hz off the on-chip 50 MHz osc/PLL | `examples/led_blink/bitstream/` | the P0 "did the bitstream take?" check (also embedded into `firmware/bringup`) |
| `blink_all.bin` | blink all available FPGA GPIO/LEDs | `test/bitstreams/v1_4/` | broader I/O sanity check |

> **Heads-up:** the upstream repo carries *two* `led_blink.bin` files with
> different hashes — `test/bitstreams/v1_4/led_blink.bin` (older, what the
> getting-started doc links to) and `examples/led_blink/bitstream/led_blink.bin`
> (the canonical one matched to `examples/led_blink/led_blink.ffpga`). On the
> Shrike-Fi the `v1_4` version drove the user LED *steady on* instead of
> blinking; switching to the `examples/...` version is what made it actually
> toggle. We use the `examples/` one here.

These are **not** part of ShrikeVault's trust base — they're scaffolding. The
wallet ships only the bitstream(s) built from `fpga/puf-prototype/` (P1+), with a
documented, reproducible build recipe.

Upstream source for `led_blink`:
[`vicharak-in/shrike` `examples/led_blink/ffpga/src/main.v`](https://github.com/vicharak-in/shrike/blob/main/examples/led_blink/ffpga/src/main.v).
