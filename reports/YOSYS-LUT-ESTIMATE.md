# Yosys Resource Estimate — FireSimMediumBoomV3Config / BaseF2Config

**Date:** 2026-08-24 (overnight autonomous run)
**Input:** `sims/firesim/sim/generated-src/f2/f2-firesim-FireSim-FireSimMediumBoomV3Config-BaseF2Config/FireSim-generated.sv` — the exact 33.8MB file AWS's Vivado build will synthesize.
**Tool:** Yosys 0.68+ (conda-forge), `synth_xilinx -family xcup` (UltraScale+ techmap — the correct family for the VU47P used by AWS F2)

---

## Result

| Resource | Count |
|---|---|
| LUT1 | 37,651 |
| LUT2 | 45,899 |
| LUT3 | 77,888 |
| LUT4 | 42,355 |
| LUT5 | 46,671 |
| LUT6 | 106,413 |
| **Total LUTs** | **356,877** |
| FDRE (flip-flop) | 154,530 |
| FDSE (flip-flop w/ sync set) | 385 |
| **Total flip-flops** | **154,915** |
| CARRY4 | 2,780 |
| MUXF7 / MUXF8 / MUXF9 | 86,577 / 34,225 / 11,879 |
| RAMB18E2 / RAMB36E2 | 273 / 195 |
| DSP48E2 | 47 |
| BUFG / BUFGCE | 17 / 2 |
| IBUF / OBUF | 1,666 / 1,641 |

Run stats: 26m45s wall-clock, 6.4GB peak RAM, 654,081 total cells across 516 modules.

## How this number was actually produced (so its limits are clear)

1. Confirmed the target file has essentially no Xilinx-specific primitives baked in already — searched for `BUFG/MMCM/PLLE/RAMB/DSP48/...` and found exactly one: `BUFGCE`, instantiated twice (once in the FASED memory timing model's clock gate, once in the clock bridge). Everything else is plain behavioral RTL, which is what let a generic synthesis tool touch it at all.
2. `BUFGCE` isn't something Yosys ships a model for, so it was stubbed as a zero-area blackbox — accurate to how it behaves in real Vivado too, since it's a dedicated clock-tree resource, not LUT fabric.
3. First full run reached the memory-technology-mapping stage and hit a real Yosys bug: one memory's read/write port width/address-bit combination didn't match any of Yosys's built-in Xilinx LUTRAM templates (`lutrams_xc5v_map.v:181`, `invalid OPTION_ABITS/WIDTH combination`). Routed around it with `-nolutram`, which forces those memories to map through Block RAM / flip-flop inference instead of distributed LUTRAM — this is why RAMB18E2/RAMB36E2 counts here should be read as **not directly comparable** to what Vivado would choose; Vivado has its own, more complete LUTRAM-vs-BRAM tradeoff logic that this workaround bypassed.

## What this number is good for, and what it isn't

**Good for:** a real, same-order-of-magnitude sanity check that this design is large — not a back-of-envelope guess, an actual technology-mapped count from the real generated RTL. It directly confirms the earlier answer given about the Zynq/50K-board question: **356,877 LUTs is roughly 11x the entire LUT budget of a 50K-class Artix-7 board (32,600 LUTs)** — not a "might be tight," a hard no, now backed by a measured number instead of an inference from general BOOM literature.

**Not a substitute for the real Vivado report, for three concrete reasons:**
1. `-nolutram` was a workaround, not the choice Vivado would make — it likely shifts some resource usage from LUTs to BRAM relative to what real synthesis would produce.
2. Yosys's ABC-based logic optimizer and Vivado's synthesis engine make different technology-mapping decisions on the same RTL — expect real Vivado numbers to differ by some margin, not match exactly.
3. This is post-synthesis, pre-place-and-route. Vivado's own synthesis (`synth_design`) report is the actual apples-to-apples comparison; anything post-implementation could shift further with retiming/optimization passes.

**Bottom line:** treat 356,877 LUTs as "this design is genuinely large, on the order of a third of a million LUTs" — solid enough to answer "does it fit on a 50K board" (no, not close) or give a rough sense of scale against the VU47P's multi-million-LUT budget, but not a number to quote as the design's final, authoritative utilization. That number arrives the moment `buildbitstream` actually runs.
