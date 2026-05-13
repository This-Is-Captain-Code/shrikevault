# `fpga/` — deferred to post-v1 (see `../docs/roadmap.md` P5)

This whole tree describes the original FPGA-based root-of-trust design
(an RO-PUF on the SLG47910 ForgeFPGA) and is **not part of ShrikeVault v1**.
v1's root-of-trust runs entirely on the ESP32-S3 — see
[`../docs/architecture.md`](../docs/architecture.md).

## Why deferred

The Renesas **ForgeFPGA Workshop** toolchain is GUI-only:
- No `--build` / `--headless` flag on `GP6.exe` / `GPLauncher.exe`.
- Yosys ships as a CLI binary, but it only produces an EDIF netlist.
- The place-and-route + bitstream packing lives inside a proprietary
  `coregp.dll` that is only invoked from the GUI.

That is incompatible with reproducible builds, CI, and the "hands-off
development from a terminal" workflow this project wants. UI-automation
of the GUI is technically possible but brittle.

## What's still useful here

- [`hello-fpga/`](hello-fpga/) — a 30-line Verilog file (and an FFW build
  recipe) that *would* produce a Shrike-Fi-friendly "blink the LED off the
  on-chip OSC" bitstream, if someone manually drives FFW for the few clicks
  needed. Good as a first build for anyone reviving the FPGA path.
- [`puf-prototype/`](puf-prototype/) — the README describes the intended
  RO-PUF Verilog (ring-oscillator bank + challenge mux + counters + SPI-target
  register interface) and the analysis approach. The Verilog itself isn't
  written yet — that's the P5 work.
- [`test/`](test/) — known-good test bitstreams from the upstream Vicharak
  `shrike` repo, useful for verifying the SPI-streaming path on the ESP32
  side (i.e. the [`../firmware/bringup/`](../firmware/bringup/) app). Note
  these designs don't *visibly* run on Shrike-Fi because they expect external
  clocks on FPGA pins 17/18 which Shrike-Fi doesn't wire — see
  [`../docs/roadmap.md`](../docs/roadmap.md) P0 notes.

## Reviving this

Path back to a working PUF root-of-trust:

1. **Either** a) write a UI-automation script (PowerShell + Windows UI
   Automation, or Python + `pywinauto`) that reliably drives FFW to build a
   bitstream from a `.ffpga` project, **or** b) an open-source toolchain for
   the SLG47910 appears (none exists today).
2. Write `puf-prototype/src/ro_puf.v` + `puf_spi_slave.v` + `top.v` +
   `tb_*.v`. Outline is in
   [`puf-prototype/README.md`](puf-prototype/README.md).
3. Build the bitstream; characterise the PUF on real silicon (uniqueness,
   reliability, min-entropy). See `../docs/roadmap.md` P5.
4. Replace the v1 `device_key` component with a PUF-driven fuzzy extractor;
   update the threat model with the stronger properties (volatile,
   silicon-bound, no-NVM key).

Until that work happens, this whole directory is reference material only.
The build status of the wallet does not depend on any of it.
