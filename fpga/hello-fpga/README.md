# `hello-fpga/` — minimum Shrike-Fi-friendly bitstream

The smallest possible "we built this ourselves and it actually works on Shrike-Fi" milestone.
Enables the SLG47910's on-chip 50 MHz oscillator, divides it down, blinks the user LED at ~1.5 Hz. Nothing else.

This exists because every example bitstream in the upstream Vicharak repo —
`led_blink`, `breathing_led`, `blink_all`, `pll_oscillator`, `counter_4bit` —
declares its design clock as `iopad_external_pin` (= an external clock pin),
and on Shrike-Fi the ESP32-S3 isn't wired to the FPGA pins that the RP2040
board uses to drive that clock. So none of them blink the LED here. **None of
those bitstreams have been tested on Shrike-Fi**, per the upstream README of
`pll_oscillator`. We need our own.

| File | Purpose |
|---|---|
| [`src/hello_fpga.v`](src/hello_fpga.v) | 30 lines of Verilog: enable OSC + divide /2^26 + drive LED |
| [`sim/tb_hello_fpga.v`](sim/tb_hello_fpga.v) | Icarus testbench — confirms `OSC_CTRL_EN`/`LED_OE` and the LED counter |
| [`BUILD.md`](BUILD.md) | Step-by-step ForgeFPGA Workshop build recipe (clicks in the IO planner) |
| `bitstream/hello_fpga.bin` | *(generated)* the 46408-byte SLG47910 bitstream |
| `hello_fpga.ffpga` | *(generated)* the FFW project file (commit alongside the .bin) |

## Run the simulation

```bash
# (Icarus Verilog must be installed: https://bleyer.org/icarus/ on Windows)
cd fpga/hello-fpga/sim
iverilog -g2012 -o sim.out tb_hello_fpga.v ../src/hello_fpga.v
vvp sim.out
```

Expect `[tb] PASS: 2 toggles` after a few seconds. The sim only checks the
*digital* counter logic — the OSC/IO-planner routing only ever exists in the
real chip.

## Why this comes before `puf-prototype/`

The PUF is a non-trivial design (~hundreds of LUTs of ring oscillators +
counters + an SPI slave register file). If a Shrike-Fi bitstream we build
doesn't blink an LED, debugging is essentially blind. `hello-fpga` is the
canary: build *this* first, see the LED toggle, then we know the OSC routing
and the FFW build flow are right, and *only then* do we drop the full PUF on it.
