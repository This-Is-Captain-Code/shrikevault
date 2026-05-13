# `fpga/` — ForgeFPGA (Renesas SLG47910) HDL

| Dir | Status | What |
|---|---|---|
| [`test/`](test/) | ✅ | known-good test bitstreams pulled from the upstream Vicharak `shrike` repo (`led_blink`, `blink_all`) — used for P0 bring-up |
| [`puf-prototype/`](puf-prototype/) | ⚪ planned (P1) | the RO-PUF: ring-oscillator bank, challenge mux, counters, MCU register slave + a sim testbench |

## The part

Renesas **SLG47910V "ForgeFPGA"**: 1120 × 5-input LUTs, 1120 FFs, ~5 kb
distributed RAM, 32 kb block RAM (2×16 kb), on-chip 50 MHz oscillator + PLL,
19 GPIO, OTP **or** SPI configuration. On the Shrike-Fi it's SPI-configured:
the ESP32-S3 streams a fixed **46408-byte** bitstream into it at every power-up.

## Toolchain — and the open-source caveat

There is **no open-source toolchain** for the SLG47910. Bitstreams are built
only with Renesas's **ForgeFPGA Workshop** (a.k.a. *Go Configure Software Hub*),
which is free-of-charge but **closed-source** for place-and-route and bitstream
packing (it uses Yosys for synthesis internally). Unlike Lattice iCE40/ECP5
there is no nextpnr target and no documented bitstream format for this part.

So in this repo, "open-source FPGA" means: **the HDL is open and auditable**, the
**build recipe is documented**, and the resulting **`.bin` is committed** — but
you are trusting Renesas's packer the same way you'd trust any closed FPGA vendor
tool. If a 100 % FOSS toolchain is a hard requirement, this is the wrong FPGA and
the design needs to move to an iCE40-class part. See
[`../docs/architecture.md`](../docs/architecture.md#open-toolchain-caveat) and
[`../docs/threat-model.md`](../docs/threat-model.md).

### Building a bitstream (outline — to be fleshed out in P1)

1. Open ForgeFPGA Workshop, create/open the project (`.ffpga`), point it at the
   Verilog in `puf-prototype/src/`.
2. Set the clock source (the on-chip 50 MHz osc / PLL) and IO planner mapping
   (which fabric signals go to which package pin — these become the MCU↔FPGA
   interconnect lines GPIO0..GPIO5).
3. Synthesise → place & route → generate the `.bin` (it will be ~46408 bytes).
4. Commit the `.ffpga` project, the generated `.bin`, and write down the exact
   tool version used, so the build is reproducible.

A Yosys/Verilator flow is fine and recommended for **simulation** of the digital
logic (counters, mux, register slave) before it ever hits the vendor tool — but
the analog-ish RO behaviour only shows up on real silicon.

## HDL conventions

Following the upstream Shrike style: a `(* top *)` module with
`(* iopad_external_pin *)` attributes on ports, `(* clkbuf_inhibit *)` on the
clock input where appropriate (see `test/`'s source in the upstream repo or
`docs/verilog_style_guide.md` there). Keep modules small and simulable.
