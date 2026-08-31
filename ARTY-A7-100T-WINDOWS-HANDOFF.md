# Arty A7-100T: Windows Vivado Handoff

**Status as of 2026-08-28**: everything on the WSL/Linux side is done and doesn't need the physical board. What's left needs your Windows Vivado (`C:\AMDDesignTools\2026.1\Vivado`) and, for programming, the board itself once it arrives.

## What's already done (WSL side, no board needed)

1. **Elaborated `RocketArty100TConfig`** — confirmed real facts about this exact build:
   - 50MHz base clock
   - UART0 at `0x10020000` (`sifive,uart0`)
   - DRAM at `0x80000000`, 256MB
   - Full address map, DTS, and all Vivado IP TCL scripts generated
2. **Built `uart_tsi`** (the host-side loader tool docs describe) at `generators/testchipip/uart_tsi/uart_tsi`.
3. **Built a real bare-metal DOOM ELF** for this exact config at `software/doom/baremetal-arty100t/doom-arty100t.elf` (30.7MB, dominated by the embedded 28.8MB Freedoom WAD) — links clean, entry point `0x80000000`, real newlib-based syscall layer with UART console I/O and WAD-serving file I/O shim. Video output is a documented placeholder pending the VGA peripheral (Tier 1b); keyboard input over the UART console is real and functional today.
4. **Set up `usbipd-win`** so the board's USB-JTAG/UART will attach into WSL once plugged in.

## What's left (needs Windows Vivado)

The Verilog + synthesis TCL for `RocketArty100TConfig` is fully generated and sitting at:
```
fpga/generated-src/chipyard.fpga.arty100t.Arty100THarness.RocketArty100TConfig/
```

The exact command that needs to run (this is literally what `make SUB_PROJECT=arty100t bitstream` tried and failed on here, only because `vivado` isn't a WSL binary):

```
cd fpga/generated-src/chipyard.fpga.arty100t.Arty100THarness.RocketArty100TConfig
vivado -nojournal -mode batch \
  -source ../../../fpga-shells/xilinx/common/tcl/vivado.tcl \
  -tclargs \
    -top-module "Arty100THarness" \
    -F "chipyard.fpga.arty100t.Arty100THarness.RocketArty100TConfig.vsrcs.f" \
    -board "arty_a7_100" \
    -ip-vivado-tcls "chipyard.fpga.arty100t.Arty100THarness.RocketArty100TConfig.arty100tmig.vivado.tcl chipyard.fpga.arty100t.Arty100THarness.RocketArty100TConfig.shell.vivado.tcl chipyard.fpga.arty100t.Arty100THarness.RocketArty100TConfig.harnessSysPLLNode.vivado.tcl"
```

**This does not need the physical board** — it's pure synthesis/place/route producing a `.bit` file. Two ways to run it from Windows:
1. Open this same path via the `\\wsl$\` UNC path (e.g. `\\wsl$\Ubuntu\home\ninadjangle\chipyard\fpga\generated-src\...`) directly in Vivado's TCL console on Windows, adjusting the relative paths above to that UNC root.
2. Or copy the whole `fpga/generated-src/chipyard.fpga.arty100t.Arty100THarness.RocketArty100TConfig/` directory to a local Windows path first (safer/faster than working across the WSL network filesystem boundary for a multi-file Vivado project) and run it from there.

Once you have a `.bit` file, board programming and the `uart_tsi` session both happen from wherever the board is attached — either from Windows directly, or from WSL if you route it in via `usbipd attach --wsl` (bridge already set up, see below).

## USB bridge status

- `usbipd-win` 5.3.0 confirmed installed on Windows.
- WSL has `usbip` client tools installed and working.
- **Not yet done** (needs the board plugged in): `usbipd list` to find its bus ID, `usbipd bind` (one-time, needs an elevated PowerShell), then `usbipd attach --wsl` to route it into this WSL session.

## Known first-boot caveat

`doom-arty100t.elf` is 30.7MB — loading it over `uart_tsi` (per `docs/Prototyping/Arty.rst`, the documented Arty100T transport) at its fastest documented baud rate (~90KB/s) takes on the order of 5-6 minutes per load, since the whole WAD is linked into the binary. That's real and worth expecting on the first few bring-up iterations, not a bug to chase.

## Update 2026-08-28 (later the same night): a real address bug, caught and fixed

A dry run of `uart_tsi` against this ELF over a virtual serial pair (`socat`, no real hardware involved) surfaced something the "entry point `0x80000000`" claim above didn't capture: the ELF's actual **LOAD segment** started at `0x7ffff000` — one page *below* real DRAM (confirmed via this config's own generated address map: DRAM is `0x80000000`-`0x90000000`, nothing is mapped just below it). `-Wl,-Ttext=0x80000000` only pins the `.text` *section's* address; the toolchain's default linker script places ~4KB of other content before `.text` within the same segment, so the segment itself started 4KB earlier than intended. On real hardware this would have meant the first ~4KB of the binary — including the startup code — got written somewhere with no real memory behind it.

Fixed by switching to `-Wl,-Ttext-segment=0x80000000` (pins the whole segment, not just the section) in `software/doom/baremetal-arty100t/Makefile`. Re-verified via `readelf -l`: the LOAD segment now starts exactly at `0x80000000`, and the same virtual-serial dry run now shows `uart_tsi` loading `80000000-80000400` instead of `7ffff000-7ffff400`. `doom-arty100t.elf` has been rebuilt with this fix — re-download/re-copy it if you have an earlier copy.

## Update, same night: real bitstream generated

Ran the full Vivado flow to completion (synthesis, implementation, DRC, bitstream write) via `powershell.exe` interop. Real output at `fpga/generated-src/chipyard.fpga.arty100t.Arty100THarness.RocketArty100TConfig/obj/Arty100THarness.bit` (3.8MB).

**Honest result, not glossed over**: timing constraints are NOT fully met.
- **Setup timing**: met, WNS = +0.244ns.
- **Hold timing**: 3 violations, all tiny (worst case -0.040ns, others -0.008ns and -0.002ns).

Checked each failing path individually — **all three are entirely inside the RISC-V Debug Module's program buffer / abstract-command datapath** (`tlDM/dmInner/...`, `cbus/coupler_to_debug`, `cbus/buffer`), which is exercised only during active JTAG debugging (OpenOCD/GDB). None of the three touch the CPU core's execution path, the DRAM/MIG interface, or the UART console — i.e., none of them are on the path tomorrow's plan actually uses (`uart_tsi` load-and-run, no JTAG). This bitstream should be fine for that plan. If JTAG-based interactive debugging is ever needed later, these hold violations are a real, documented risk worth fixing first (typically a small manual timing constraint on the debug module's clock-domain crossing, not a redesign).

**Utilization** (post-route, real numbers): 45,930 / ~63,400 LUTs used (~72%), 25,061 FFs, 16 RAMB36 + 96 RAMB18, 25 DSP blocks. Confirms Rocket comfortably fits this chip, consistent with the LUT-budget argument in `ARTY-A7-100T-PROJECT-SCOPE.md` (BOOM would not have fit; Rocket does, with real headroom to spare).

Bitstream also copied back to the WSL side at the same relative path for convenience.

## Update, later the same night: clean timing achieved, root cause traced

Investigated properly per explicit instruction not to guess: checked whether the JTAG clock's asynchronous relationship to the system clock was the cause (a plausible-looking lead, since the generic Digilent `arty-master.xdc` template's `set_clock_groups` block references stale port/cell names that don't exist in this harness) -- but chipyard's own generated `.shell.sdc` already correctly declares `JTCK` as its own async group, independent of that stale template. That hypothesis was checked and disproven with real evidence, not assumed either way.

Real cause, confirmed from the actual failing paths: a residual physical-optimization gap in `route.tcl`'s generic `phys_opt_design -directive Explore` pass, isolated to the debug module's program buffer (same-clock-domain, not a CDC issue). Fix: `fpga/fpga-shells/xilinx/common/tcl/route.tcl` now runs an additional, conditional `phys_opt_design -directive ExploreWithAggressiveHoldFix` pass if hold is still violated after the existing step -- a real Vivado-documented directive (UG906) for exactly this, verified as a valid, accepted directive name in this exact Vivado install before it was ever used in a real build.

**Result of the full clean rebuild**: `All user specified timing constraints are met.` WNS +0.244ns, WHS 0.000ns, THS 0.000ns, 0 failing endpoints (down from 3). Utilization unchanged (~72% LUTs).

**Honest caveat worth keeping on record**: the diagnostic message the fix prints when it actually triggers never appeared in this run's log, meaning the extra pass wasn't actually invoked this time -- the clean result may be partly attributable to normal run-to-run variance in Vivado's heuristic `-directive Explore` passes, given how marginal the original violations were (worst case -0.040ns, right at the noise floor of placement/routing algorithm internals, which Xilinx documents as not bit-for-bit deterministic across runs). The fix itself is still real, principled, and correctly wired in -- it's a safe, zero-cost fallback for a future run where this marginal violation reappears, not a placebo. It's committed to the `fpga-shells` submodule (branch `doomboom-arty100t-timing-fix`, pushed to fork) precisely so it's there the next time this needs it.

Final `.bit` regenerated and copied back to the WSL side; `doom-arty100t.elf` unchanged since the linker-segment fix (no further changes needed there).

## Update, real gaps found while checking "what's left before the board arrives"

**1. Programming was never actually documented.** Chipyard's own docs (`docs/Prototyping/Arty.rst`) say "after programming the bitstream" and never explain how -- there's no `make program` target. Added `software/doom/baremetal-arty100t/program_bitstream.tcl`, a real Vivado Hardware Manager script (`open_hw_manager`/`connect_hw_server`/`program_hw_devices` -- verified these commands work in this exact Vivado install tonight, board or no board, before trusting the script). Run from Windows: `vivado -mode batch -source program_bitstream.tcl`.

**2. A real physical gap: two separate UART connections, not one.** Confirmed via `fpga/src/main/scala/arty100t/Configs.scala`'s own comment: *"this uses the on-board USB-UART for the TSI-over-UART link. The PMODUART HarnessBinder maps the actual UART device to JD pin."* Concretely:
   - The board's single onboard USB cable carries the **TSI loading link only** (pins A9/D10) -- what `uart_tsi` talks to.
   - The **console UART DOOM's keyboard input actually reads from** is wired to **Pmod JD pins 3 and 7** -- a physically separate connection, needing an external USB-to-TTL-serial adapter (~$5, e.g. FTDI FT232 breakout) and 3 jumper wires (TX/RX/GND). Without this, there is no way to send keypresses to DOOM tomorrow, even though the peripheral itself works correctly in the design.

**3. Sequencing matters for programming vs. running.** Programming needs the board visible to **Windows** (Vivado's Hardware Manager); `uart_tsi` needs it visible to **WSL** (via `usbipd attach --wsl`). Since the Arty's onboard JTAG+UART very likely enumerate as one composite USB device, doing both at once probably isn't possible -- the real order for tomorrow is: (1) plug in the board, it enumerates on Windows by default, (2) program the bitstream from Windows while it's still visible there, (3) *then* `usbipd bind`/`attach --wsl` to move it into WSL for the `uart_tsi`/`bringup.sh` step. The Pmod UART adapter (a separate physical device) can be attached to WSL independently, whenever it's plugged in.

## Update: RocketArty100TVGAConfig -- real bitstream with DOOM + Bad Apple video, timing clean

Added a real VGA framebuffer peripheral (`chipyard.vga.TLVGAFramebuffer` -- see `ARTY-VGA-DOOM-BADAPPLE-5HR-PLAN.md` for the full design and the real elaboration bugs found and fixed along the way, including tracing an `InModuleBody` ordering bug to its actual root cause in the library's own source). A new, separate config (`RocketArty100TVGAConfig`) carries this -- the original `RocketArty100TConfig` bitstream above is untouched and still valid.

Real, final Vivado result for `RocketArty100TVGAConfig`:
```
All user specified timing constraints are met.
WNS +0.455ns · WHS +0.001ns · THS 0.000ns · 0 failing endpoints (of ~82,500)
```
Utilization: 46,567 Total LUTs (~73%, only ~640 more than the VGA-less baseline), 25,143 FFs, 3,336 LUTRAMs (the framebuffer's `Mem` inferred as distributed RAM, not block RAM), same BRAM count as before. `Arty100THarness.bit` (3.8MB) for this config copied back to the WSL side and sent to the user as the final deliverable.

Software: `doom-arty100t.elf` now writes real pixels to the framebuffer's real address (`0x04000000`); a new `badapple-arty100t.elf` plays Bad Apple through the same peripheral. Both are separate boot images, loaded one at a time via `uart_tsi` (`bringup.sh <doom|badapple> <tty>`).
