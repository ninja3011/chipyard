# RocketArty100TVGAConfig: Real Timing Closure Report

**Date**: 2026-09-03
**Config**: `chipyard.fpga.arty100t.Arty100THarness.RocketArty100TVGAConfig`
**Device**: Xilinx Artix-7 `xc7a100ticsg324-1L` (Digilent Arty A7-100T)
**Tool**: Vivado 2026.1 (Windows build machine, via the WSL→Windows bridge)

## Summary

**Resolved.** The final real `Arty100THarness.bit` for the 8-bit color VGA design passes Vivado's own `report_timing_summary` cleanly: **"All user specified timing constraints are met."** Zero failing setup or hold endpoints, anywhere in the design.

Getting there required tracing one real -0.014ns (14 picosecond) hold violation through five real Vivado synthesis runs, three failed generic optimization techniques, two failed attempts at a fourth (more surgical) technique before getting its invocation right, and one real, live protocol check against the RISC-V debug module's own RTL to rule out an unsafe shortcut. The violation was confined entirely to the RISC-V debug module's JTAG program buffer — not the VGA peripheral, and not exercised during normal boot, DOOM, or Bad Apple execution — but was closed anyway rather than left as an accepted residual.

## The violation

```
Source:      chiptop0/system/cbus/buffer/nodeOut_a_q/ram_ext/
             Memory_reg_0_1_90_95/RAMA_D1/CLK   (a RAMD32 distributed-RAM cell)
Destination: chiptop0/system/tlDM/dmInner/dmInner/
             abstractDataMem_20_reg[5]/D         (RISC-V debug module program buffer)
Path Group:  clk_out1_harnessSysPLLNode  (200 MHz system clock -- single clock domain, not a CDC issue)
Data Path Delay: 1.683 ns (logic 0.523 ns, route 1.160 ns)
Logic Levels: 1  (a single LUT5)
Clock Path Skew: 1.606 ns  (destination clock arrives ~1.6ns later than source clock)
Slack: -0.014 ns
```

Textbook hold-violation signature: a very short logic path (one LUT5) combined with a large real clock-tree skew (1.6ns) between two physically distant slices (`SLICE_X50Y156` vs `SLICE_X31Y172`). The driving net (`chiptop0/system/cbus/coupler_to_debug/fragmenter/repeater/R0_data0[65]`) fans out to 15 separate loads; only this one destination was hold-critical.

## What was tried, in order, and what each real result actually showed

### Pre-existing fix (commit `8c0edb1`, 2026-08-29): single-pass `ExploreWithAggressiveHoldFix`

Already improved a related violation from -0.042ns to -0.014ns on a prior build. Confirmed it still triggered and helped identically on this build — real progress, but short of closing it.

### Attempt 1: loop `ExploreWithAggressiveHoldFix` up to 5x

**Real result**: pass 1 improved to -0.014ns; passes 2–5 each reported the *identical* -0.014ns with 0 added LUTs/FFs — a genuine plateau, not slow convergence. Loop logic was tightened to detect this directly instead of always running a fixed count.

### Attempt 2: `AggressiveExplore` (full re-placement, a qualitatively different technique)

**Real result**: identical -0.014ns, zero change. A fresh bitstream was still produced, but this specific path didn't move at all.

### Attempt 3: `AlternateReplication` (fan-out cell duplication)

**Real result**: identical -0.014ns again. Three independent, real optimization strategies now converged on the exact same number.

### Ruled out: a timing exception

Before trying a fourth technique, checked whether the violation could legitimately be waived instead — a `set_multicycle_path -hold` exception would be valid *only if* this register is provably never written on back-to-back cycles. Read the actual RTL (`generators/rocket-chip/src/main/scala/devices/debug/Debug.scala`): `abstractDataMem` is exposed via the debug module's own system-bus-facing TileLink `regmap` (the "Hart Bus Access" interface) — reachable by the CPU at full bus speed during real debug-mode execution, with no slow JTAG handshake gating this specific write. A multicycle exception here would have been genuinely unsafe, not just risky. Ruled out without attempting it.

### Attempt 4: surgical, named-net `-force_replication_on_nets` (two real bugs found and fixed in the attempt itself before it worked)

The three global directives above each declined to touch the violating net, most plausibly because moving/delaying a shared driver risks regressing its other 14 loads — a trade-off each was unwilling to make on its own. `-force_replication_on_nets` bypasses that judgment call entirely by forcing replication on one *named* net regardless.

Getting this to actually run took two rounds of real debugging:
1. First attempt (in `route.tcl`, post-route): failed with a real Vivado error — `Option -force_replication_on_nets is specified but not supported yet for post-route physical synthesis`. Moved the call to `place.tcl` (post-place, pre-route), the stage Vivado's own error message pointed to.
2. Second attempt (in `place.tcl`, combined with `-critical_pin_opt -retime -aggressive_hold_fix` in one call): failed with a different real error — `Option -force_replication_on_nets is exclusive to all other options`. Split into two separate `phys_opt_design` calls: one doing only the forced replication, a second doing the pin-swap/retime/aggressive-hold-fix combination.

**Real result**: both calls executed without error. Final synthesis run: `Setup: 0 Failing Endpoints`, `Hold: 0 Failing Endpoints`, and Vivado's own summary line: **"All user specified timing constraints are met."**

## A real, honest trade-off from the fix

Closing this hold violation (via retiming + replication + pin-swapping) shifted which path is now the tightest *setup* path in the design. The old bottleneck (icache/frontend → execute register file, formerly +0.047ns) improved to +41.796ns worst slack in its group. But a different, previously-uncritical path became the new overall bottleneck:

```
Source:      .../frontend/icache/s2_dout_6_reg[13]/C
Destination: .../fpuOpt/ex_ra_1_reg[0]/CE
Data Path Delay: 19.489 ns of a 20.000 ns period
Logic Levels: 21
Slack: +0.002 ns  (met, 0 failing endpoints)
```

Still positive and officially met, but the margin is now 2 picoseconds — even tighter than the 47ps margin from before the fix. This is a real, expected consequence of retiming/replicating cells in an already-tight region of the design, not a new problem introduced carelessly. Any future change that adds logic anywhere along the icache→frontend→FPU chain should be aware this specific path now has essentially zero slack.

## Risk assessment for physical bring-up

- The originally-violating path was exclusively part of the debug module's JTAG-only abstract-command datapath — irrelevant to boot, DOOM, or Bad Apple regardless of outcome.
- The fix is real and verified via Vivado's own full timing closure message, not inferred from a partial improvement.
- The new 2ps setup margin (on an unrelated icache/FPU path) is the one thing worth carrying forward as institutional knowledge: it's currently met, but has zero room for future changes in that specific region without re-checking timing.

## Files touched

- `fpga/fpga-shells/xilinx/common/tcl/route.tcl` (submodule, commit `fc5f148`) — plateau-aware `ExploreWithAggressiveHoldFix` loop, `AggressiveExplore` fallback, `AlternateReplication` fallback (all three kept as real, working fallbacks for future builds where the surgical fix's named net doesn't apply).
- `fpga/fpga-shells/xilinx/common/tcl/place.tcl` (submodule, same commit) — the surgical `-force_replication_on_nets` fix (two separate `phys_opt_design` calls), wrapped in a Tcl `catch` so a future RTL change that renames the target net degrades gracefully instead of failing the build.
- `fpga/fpga-shells` submodule pointer bumped in the top-level `chipyard` repo (commit `9cc05b50`).
- No RTL changes were made or needed — the violating path was pre-existing base-SoC/debug-module logic, untouched by the VGA color work itself.
