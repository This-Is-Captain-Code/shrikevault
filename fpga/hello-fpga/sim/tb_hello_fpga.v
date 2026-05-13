/*
 * tb_hello_fpga.v — Icarus/Verilator testbench. Drives `clk` at the design's
 * intended 50 MHz, verifies OSC_CTRL_EN/LED_OE go high and the LED bit
 * toggles roughly every 0.67 s (= 2^25 / 50e6).
 *
 * Run:    iverilog -g2012 -o /tmp/sim tb_hello_fpga.v ../src/hello_fpga.v && vvp /tmp/sim
 */
`timescale 1ns/1ps

module tb_hello_fpga;
    reg  clk = 1'b0;
    wire OSC_CTRL_EN, LED, LED_OE;

    hello_fpga dut (.clk(clk), .OSC_CTRL_EN(OSC_CTRL_EN), .LED(LED), .LED_OE(LED_OE));

    // 50 MHz = 20 ns period
    always #10 clk = ~clk;

    integer led_toggles = 0;
    reg last_led = 1'b0;

    initial begin
        $display("[tb] start");

        // Let the design settle for one clock so registers initialise.
        #25;
        if (OSC_CTRL_EN !== 1'b1) begin $display("[tb] FAIL: OSC_CTRL_EN should be high");  $finish; end
        if (LED_OE      !== 1'b1) begin $display("[tb] FAIL: LED_OE should be high");       $finish; end
        $display("[tb] OSC_CTRL_EN=1 ✓  LED_OE=1 ✓");

        // Run long enough to see at least two LED toggles. With bit 25 of a
        // 50 MHz counter, one toggle ≈ 0.67 s of simulated time, so we need
        // ~2^26 clocks = ~67 million cycles. (Sim is fast — finishes in a few s.)
        last_led = LED;
        repeat (2 * (1 << 25) + 1000) begin
            @(posedge clk);
            if (LED !== last_led) begin
                led_toggles = led_toggles + 1;
                $display("[tb] LED toggle #%0d  at  cycle ~%0d   LED=%b", led_toggles, $time/20, LED);
                last_led = LED;
            end
        end

        if (led_toggles >= 2) $display("[tb] PASS: %0d toggles", led_toggles);
        else                  $display("[tb] FAIL: %0d toggles (expected >=2)", led_toggles);
        $finish;
    end
endmodule
