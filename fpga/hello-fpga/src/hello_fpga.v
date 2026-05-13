/*
 * hello_fpga.v — minimum Shrike-Fi-friendly Verilog: enable the on-chip 50 MHz
 *                oscillator and blink the user LED at ~1.5 Hz.
 *
 * Pin/signal naming matches what the upstream Vicharak walkthrough uses, so
 * the FFW IO Planner mapping is the canonical one:
 *
 *   clk     ─ OSC_CLK     (OSC clock output, internal — *not* an external pin)
 *   clk_en  ─ OSC_EN      (drives the OSC's enable; constant 1 turns OSC ON)
 *   LED     ─ GPIO16_OUT  (FPGA pin 7 / GPIO16 = Shrike-Fi user LED, active high)
 *   LED_en  ─ GPIO16_OE   (OE for the LED pad)
 *
 * The Shrike-Fi only wires FPGA pins 3–6 (the SPI bus) and GPIO16 (LED).
 * Pins 17/18 (which the RP2040 board uses) are NOT connected on Shrike-Fi —
 * which is why upstream example bitstreams that route `clk` to an external pin
 * don't blink on this board. This module routes `clk` from the internal OSC.
 */
(* top *) module hello_fpga (
    (* iopad_external_pin, clkbuf_inhibit *) input  clk,     // ← FFW IO planner: OSC_CLK
    (* iopad_external_pin *)                 output clk_en,  // ← FFW IO planner: OSC_EN
    (* iopad_external_pin *)                 output LED,     // ← FFW IO planner: GPIO16_OUT
    (* iopad_external_pin *)                 output LED_en   // ← FFW IO planner: GPIO16_OE
);

    // Enable the on-chip 50 MHz oscillator. This is the *one bit* that's
    // effectively missing from the upstream example .bin files we tried —
    // they ship with `clk_en` unmapped (or mapped to a no-op pin) so the OSC
    // never turns on, the counter never ticks, the LED never moves.
    assign clk_en = 1'b1;

    // Drive the LED pad's output enable.
    assign LED_en = 1'b1;

    // /2^26 divider: bit 25 of a 50 MHz counter toggles every ~0.67 s,
    // for one full on→off→on cycle every ~1.3 s. Visible.
    reg [25:0] cnt = 26'b0;
    always @(posedge clk) cnt <= cnt + 1'b1;

    assign LED = cnt[25];

endmodule
