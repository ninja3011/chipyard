/* verilator lint_off UNOPTFLAT */
//
// PATCHED FOR BASYS3/VIVADO -- hand-edited, not regenerated.
// This ASIC-style latch-based clock gate is the only instance of
// EICG_wrapper in this design (grep confirms exactly one: ChipTop.sv's
// "gated_clock_debug_clock_gate", gating only the Debug Module's clock
// domain based on dmactive). Implemented as real fabric logic, it put a
// LUT+latch directly in the clock tree, which Vivado's placer routed with
// enough skew to cause 73 hold violations elsewhere in the design
// (Design Timing Summary: WHS -0.138ns, THS -2.152ns, all on
// clk_out1_clk_wiz_0 -- see report_methodology's "TIMING-14: LUT on the
// clock tree"). Since DmiAutoloader's very first DMI write sets
// dmcontrol.dmactive=1 and it stays 1 for the rest of runtime, `en` is
// asserted almost immediately after reset and never deasserts again in
// this design's actual usage -- there is no real power-gating benefit
// being lost by making this a plain pass-through, only the ASIC-style
// glitch-free-gating discipline this cell exists for, which isn't needed
// here since `en` never toggles during active operation.
//
// If you regenerate vivado_sources/ from a fresh `make verilog` run, this
// file will be overwritten with the original latch-based version -- redo
// this same edit (or copy this file back in) before re-synthesizing.

module EICG_wrapper(
  output out,
  input en,
  input test_en,
  input in
);

  assign out = in;

endmodule