# Arty A7-100T board day: readiness plan

**Written**: 2026-09-02. **Updated**: 2026-09-03, after both real blockers below were closed and verified.
**Goal**: walk in, plug in, hit play.

## Status: both real blockers are closed. This is ready for physical bring-up.

The two blockers this plan originally tracked are both done and verified, not just attempted:

1. **The bitstream is real, current, and timing-clean.** `Arty100THarness.bit` (built 2026-09-03, `RocketArty100TVGAConfig`) reflects the exact committed RTL -- confirmed via `git diff HEAD` showing zero drift between the RTL that produced this bitstream and what's committed. Vivado's own summary: **"All user specified timing constraints are met"** -- zero failing setup or hold endpoints, after real investigation and a real fix for a residual -0.014ns hold violation (see `ARTY-VGA-TIMING-VIOLATION-REPORT.md` for the full story). The peripheral is real 8-bit color (3-3-2 RGB, not the original 16-bit design -- dropped to 8bpp partway through when the 16-bit version hit a real Arty A7-100T Block RAM ceiling; see `VGAFramebuffer.scala`'s header comment).
2. **DOOM and Bad Apple's software write the current color format.** `doomgeneric_arty100t.c` and `badapple_arty100t.c` both write real 8-bit (3-3-2 RGB) pixels, confirmed via `git diff HEAD` showing the on-disk source, the built ELFs, and the committed source are all identical -- no stale build. `doom-arty100t.elf` and `badapple-arty100t.elf` are current.

Everything RTL/software/synthesis-side is committed: `doom-challenge-phase1` branch (chipyard repo) and the `fpga-shells` submodule (`doomboom-arty100t-timing-fix` branch).

## What's left: physical wiring only

### Hardware needed
- [x] Arty A7-100T board
- [x] Pmod VGA module -- **confirmed arrived**
- [x] FT232RL USB-to-serial adapter + jumper wires -- **confirmed arrived**
- [x] VGA cable
- [x] VGA-to-HDMI converter
- [x] HDMI monitor
- [ ] USB-Micro cable, board-to-PC -- confirm in hand (does double duty: programming *and* TSI loading)
- [ ] **USB Mini-B cable, FT232RL-to-PC -- confirmed NOT a Micro-USB port.** The specific FT232RL module in hand (Robu SKU 9707) lists "Mini USB Port Connection" on its own product page -- a different, wider/trapezoidal connector than the Arty board's own Micro-USB port. These are not interchangeable and Mini-USB cables aren't as commonly on hand anymore (common circa PS3 controllers/older digital cameras, largely superseded since). **Real risk of blocking bring-up if not sourced before board day.**

### Physical wiring
- [ ] **Pmod VGA onto JB *and* JC together** (not JA -- that was the old monochrome mapping; color needs the full 14-pin real mapping across both headers, confirmed from Digilent's own reference design).
- [ ] VGA cable: Pmod VGA -> VGA-to-HDMI converter -> HDMI monitor.
- [ ] FT232RL wired to **Pmod JD pins 3 and 7** (TX/RX) + a GND pin -- this is the *separate* console link DOOM's keyboard input reads from; the board's onboard USB only carries the TSI load link, not this.
- [ ] FT232RL's own USB side plugged into the PC, ready for its own `usbipd attach` once needed.

### Sequencing (do in this order)
1. Plug the Arty board's USB-Micro cable in. Confirm it enumerates on **Windows** (Device Manager) -- not yet routed into WSL.
2. **Program the bitstream from Windows** (Vivado Hardware Manager needs to see the board directly):
   ```
   vivado -mode batch -source C:/arty100t-build/chipyard/software/doom/baremetal-arty100t/program_bitstream.tcl
   ```
   (Already mirrored to the Windows build machine and pointed at `RocketArty100TVGAConfig` -- confirmed identical to the WSL source via `diff`.)
3. Once programmed, route the board into **WSL**: `usbipd list` -> `usbipd bind --busid <id>` (elevated PowerShell, one-time) -> `usbipd attach --wsl --busid <id>`.
4. Plug in the FT232RL, route it into WSL too (separate `usbipd attach` for its own device).
5. Load and run, from WSL:
   ```
   cd software/doom/baremetal-arty100t && ./bringup.sh doom /dev/ttyUSBx
   ```
   (or `badapple` instead of `doom`). `uart_tsi` is already built and executable. Expect ~5-6 minutes real load time for DOOM (30.7MB ELF, dominated by the embedded 28.8MB WAD) -- that's normal, not a hang.
6. Watch the monitor. If nothing appears: check the Pmod VGA is fully seated on *both* JB and JC (a half-seated double-Pmod module is a classic real failure mode), and confirm the FT232RL's TX/RX aren't swapped (harmless to swap and retry).

## Known, accepted residual risk (not a blocker)

The timing-closure fix (see the timing violation report) shifted the design's tightest *setup* path elsewhere in the Rocket core's icache/frontend/FPU region, down to **2 picoseconds** of margin -- still officially met (0 failing endpoints), but with essentially zero room left. This has no bearing on tonight's bring-up; it's a note for **future RTL changes only** -- if anything is ever added along that specific path, timing should be re-checked before trusting a new build blind.

## What's already real and doesn't need re-doing
- The address-decode fix, the full-SoC integration test, the CPU regression, coverage, and the real Bad Apple/DOOM/DOOM-gameplay simulation verification from earlier work all still apply -- they tested the underlying TileLink/scan-out logic, which the color upgrade widened but didn't restructure.
- The `usbipd`/Windows bridge setup is confirmed working across many real rebuilds tonight -- no new setup needed, just the sequencing above.
- The real Pmod VGA pinout (JB+JC, 14 pins) is confirmed from two independent sources (Digilent's own reference design, cross-checked against the local `arty-master.xdc`) and is exactly what's baked into the current bitstream's XDC constraints -- verified via the real generated `.shell.xdc` showing all 14 `PACKAGE_PIN`/`IOSTANDARD` entries.
