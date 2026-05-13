# Building `hello_fpga.bin` with ForgeFPGA Workshop

This walks through producing a Shrike-Fi-friendly bitstream from
[`src/hello_fpga.v`](src/hello_fpga.v). Once it's built, drop the `.bin` into
[`../../firmware/bringup/main/bitstreams/`](../../firmware/bringup/main/bitstreams/),
rebuild the firmware, flash, and run `fpga_load hello_fpga` on the console —
the FPGA's user LED should blink at ~1.5 Hz. **That is the milestone this file
is here to deliver:** the first bitstream we've actually built ourselves that
works on Shrike-Fi (not borrowed from the upstream repo).

## 0. Prereqs

- **Renesas Go Configure Software Hub** installed, with the **ForgeFPGA**
  device family added. (See `../../README.md` § Toolchains, or
  [Vicharak's tools-setup guide](https://vicharak-in.github.io/shrike/tools_setup_guide.html).)
- The Shrike-Fi powered up and our `firmware/bringup` flashed on the ESP32-S3
  (so we can stream the new bitstream into the FPGA over USB).

## 1. New project

1. Open Go Configure Software Hub → **New Project** → choose **SLG47910V** → ForgeFPGA Workshop opens.
2. **Save As** `hello_fpga.ffpga` in this directory (`fpga/hello-fpga/`).

## 2. Import the Verilog

- In the project tree, right-click the **HDL** / **Source** node → **Add Existing File** → pick `src/hello_fpga.v`.
- Mark `hello_fpga` as the **top module**.

## 3. Wire the on-chip OSC to `clk`  *(the part the upstream examples got wrong for Shrike-Fi)*

This is the critical step. In the **IO Planner / Clock Routing** tab:

| Design net | Where it goes |
|---|---|
| `clk` *(input)* | the **internal 50 MHz oscillator** output (NOT an external pin). In FFW this is "OSC" → drag a wire from the OSC block's `CLK_OUT` to the `clk` net. |
| `OSC_CTRL_EN` *(output)* | the OSC's `EN` control input. (FFW shows this as the OSC's enable pin.) |
| `LED` *(output)* | **FPGA pin 7 — package "GPIO16"** — this is the Shrike-Fi user LED (active high). |
| `LED_OE` *(output)* | the **output-enable** for the LED pad. |

Leave every other FPGA pin **unassigned** — Shrike-Fi only wires pins 3–6 (the SPI bus) and the LED, so anything else is unconnected.

## 4. PLL / clock config

Open **PLL Configurator**:
- `pllFref` = `50.0` (MHz — the internal OSC)
- `pllClockSelection` = **internal OSC** (in this dropdown's terms — *NOT* "external pin")
- Leave bypass / dividers at defaults; we're not using the PLL output, the design uses the raw 50 MHz OSC.

> If you can't figure out which dropdown value means "internal OSC", build it
> both ways and try both bitstreams on the board — one will blink, the other
> won't. Note which one worked in a comment back in this BUILD.md.

## 5. Synth → Place & Route → Bitstream

- **Synthesize** → **Place & Route** → **Generate Bitstream**.
- Output goes to `hello_fpga.ffpga`'s sibling — copy it to `bitstream/hello_fpga.bin`.
- Sanity check: size must be exactly **46408 bytes** (the fixed SLG47910 SRAM config size). If it's not, something is off.

## 6. Drop it into the firmware and flash

```powershell
# from repo root:
cp fpga/hello-fpga/bitstream/hello_fpga.bin firmware/bringup/main/bitstreams/

# then edit firmware/bringup/main/CMakeLists.txt to add:
#   "bitstreams/hello_fpga.bin"  to EMBED_FILES
# and firmware/bringup/main/main.c's s_bitstreams[] table to add an entry.

& "$env:USERPROFILE\esp\esp-idf\export.ps1"
idf.py -C firmware/bringup build
idf.py -C firmware/bringup -p COM19 flash
```

Then on the console: `fpga_load hello_fpga` — **the FPGA's user LED should blink at ~1.5 Hz** (one full on/off cycle every ~1.3 s).

## 7. Things that can go wrong

| Symptom | Likely cause |
|---|---|
| LED steady ON | `LED_OE` not asserted (forgot to drive it) or `cnt[25]` is a power-up X and synth tied it high — check the synth report. |
| LED steady OFF / FAINT | OSC not enabled. Re-check that `OSC_CTRL_EN` is wired to the OSC's `EN` input in the IO planner, and that `pllClockSelection` is "internal OSC". |
| `.bin` is the wrong size (not 46408) | Wrong device selected (must be SLG47910V), or PnR didn't finish. |
| Build succeeds but the firmware load logs no errors yet the LED doesn't change | This is what the upstream bitstreams did to us. Re-check the OSC-to-`clk` routing in the IO planner. |

When it blinks, **commit `src/hello_fpga.v` + `bitstream/hello_fpga.bin` + the
`.ffpga` project file together** so the build is reproducible by anyone with
the same FFW version.
