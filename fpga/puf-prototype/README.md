# `puf-prototype/` — RO-PUF on the ForgeFPGA (planned, P1)

> Not started yet — this is a placeholder describing what goes here. See
> [`../../docs/roadmap.md`](../../docs/roadmap.md) (P1) and
> [`../../docs/architecture.md`](../../docs/architecture.md) (the PUF section).

## What it will contain

```
puf-prototype/
  src/
    ro_puf.v          parameterised ring-oscillator bank (N ROs from one macro),
                      challenge mux (pick pair i,j), edge counters, fixed
                      measurement window off the on-chip 50 MHz reference clock
    puf_regslave.v    tiny MCU-facing register interface on FPGA GPIO0..GPIO5
                      (= ESP32-S3 GPIO35..40): write challenge, pulse start,
                      read back two counters; no persistent secret state
    top.v             (* top *) wrapper + IO-pad attributes (Shrike HDL style)
  sim/
    tb_ro_puf.v       Icarus/Verilator testbench for the *digital* logic
                      (mux, counters, regslave handshake) — RO analog behaviour
                      only shows on real silicon
  puf.ffpga           ForgeFPGA Workshop project (committed alongside the .bin)
  bitstream/
    ro_puf.bin        generated 46408-byte bitstream (committed, with the
                      tool-version recipe in a sidecar note)
```

## Design notes (carried over from architecture.md so they're not lost)

- Start with **N ≈ 64** ring oscillators; scale toward 128 as the LUT budget
  allows (a short inverter chain + enable is a handful of LUTs; 1120 LUTs is
  plenty for the bank + counters + mux + regslave).
- A challenge selects pair (i, j); one response bit = sign of
  `count(RO_i) − count(RO_j)` over a fixed reference window. We expose the raw
  *counts*, not just the bit, so the host can rank stable/unbiased pairs and
  estimate noise — the fuzzy extractor's helper data then records which
  pairs/bits to use.
- **We can't pin RO placement** (closed Renesas PnR) → routing asymmetry will
  bias/destabilise some pairs. That's expected; P1's job is to *measure* it
  (`host-tools/puf_analyze.py`) and size the ECC accordingly, not to assume it
  away. If the bank is too weak: more ROs / a TERO-or-loop PUF / (last resort)
  treat the PUF as one entropy input mixed with eFuse + on-chip TRNG — that last
  one weakens the "key from silicon alone" story and would be re-confirmed first.
- Keep the FPGA dumb: it produces transient counts, nothing more. All the secret
  handling (fuzzy extraction, KDF, key hierarchy) is on the ESP32-S3 — see the
  threat model for why that's a deliberate, acknowledged limit, not an oversight.

## Toolchain

Built only with Renesas **ForgeFPGA Workshop** (closed-source PnR/packing; Yosys
synthesis under the hood). No nextpnr / open bitstream for the SLG47910. Simulate
the digital logic with Icarus/Verilator first; commit the `.ffpga` project + the
generated `.bin` + the exact tool version so the build is reproducible. See
[`../README.md`](../README.md) for the open-toolchain caveat.
