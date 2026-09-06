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
- [ ] **Confirm the Arty's USB-Micro cable is a real data cable, not power-only.** Some bundled/charging Micro-USB cables have no data lines wired -- JTAG programming needs data. Use one known to have synced a phone or transferred files, not a random charger cable.
- [x] VGA-to-HDMI converter power -- **confirmed unpowered/dongle-style**, plugs directly into the monitor's HDMI port with no separate cable. Draws its (small) power straight from the HDMI connector's own +5V pin, a legitimate and common design for this form factor -- no external power source needed.
- [x] Standalone HDMI cable -- **not needed**, converter plugs directly into the monitor.
- [x] **Arty board's own power -- decided: run on USB power, no separate brick.** No power brick on hand, and not worth sourcing this close to board day (support lines closed 4th Sept, normal shipping wouldn't arrive in time regardless). The same USB-Micro cable already required for JTAG/TSI delivers power by default -- nothing extra needed to try. Real risk (untested DDR3-heavy design) accepted knowingly: if the board resets unexpectedly or behaves erratically mid-load (not a clean "nothing happens" failure, which would point elsewhere), that's the diagnostic signal to revisit external power -- not something to solve preemptively tonight.

### Physical wiring
- [ ] **Pmod VGA onto JB *and* JC together** (not JA -- that was the old monochrome mapping; color needs the full 14-pin real mapping across both headers, confirmed from Digilent's own reference design).
- [ ] VGA cable: Pmod VGA -> VGA-to-HDMI converter -> HDMI monitor.
- [ ] **Set the FT232RL's voltage-selector jumper to 3.3V before wiring anything.** The module (Robu SKU 9707) supports both 3.3V and 5V logic levels; the Arty's Pmod headers are 3.3V-only (LVCMOS33, confirmed via the real generated XDC). Feeding 5V logic into a 3.3V-only FPGA pin risks real damage -- check this physically before connecting.
- [ ] FT232RL wired to **Pmod JD pins 3 and 7** (TX/RX, crossed -- module TXD to one pin, RXD to the other) + a shared GND pin -- this is the *separate* console link DOOM's keyboard input reads from; the board's onboard USB only carries the TSI load link, not this. Do **not** connect the module's VCC to the Pmod header -- the Arty board has its own power; only the two data lines plus ground are needed.
- [ ] FT232RL's own USB side (Mini-USB, see above) plugged into the PC, ready for its own `usbipd attach` once needed.

### Do tonight, while internet access is easy
- [ ] **Plug the FT232RL into Windows once and confirm the driver installs.** If this exact adapter has never been plugged into this PC, Windows needs to recognize it as a real COM port (FTDI VCP driver) before `usbipd` can do anything with it. Check Device Manager -> Ports (COM & LPT) shows something like "USB Serial Port (COMx)" -- not sitting under "Unknown devices" or "Other devices." If the driver doesn't auto-install, grab it from FTDI's own site now rather than mid-bring-up tomorrow.

### Two real days, not one: the monitor doesn't arrive until Sunday

Everything below through step 5 is fully doable **tomorrow, with no monitor** -- confirmed real console output takes the place of visual confirmation until Sunday.

1. Plug the Arty board's USB-Micro cable in. Confirm it enumerates on **Windows** (Device Manager) -- not yet routed into WSL.
2. **Program the bitstream from Windows** (Vivado Hardware Manager needs to see the board directly):
   ```
   vivado -mode batch -source C:/arty100t-build/chipyard/software/doom/baremetal-arty100t/program_bitstream.tcl
   ```
   (Already mirrored to the Windows build machine and pointed at `RocketArty100TVGAConfig` -- confirmed identical to the WSL source via `diff`.) Real success here also definitively confirms the USB-Micro cable carries data, settling that open question for good.
3. Once programmed, route the board into **WSL**: `usbipd list` -> `usbipd bind --busid <id>` (elevated PowerShell, one-time) -> `usbipd attach --wsl --busid <id>`.
4. Wire the FT232RL (3.3V jumper set, TX/RX crossed to Pmod JD pins 3/7, shared GND, no VCC), confirm its Windows driver, plug in, route it into WSL too (separate `usbipd attach`).
5. Load and run, from WSL:
   ```
   cd software/doom/baremetal-arty100t && ./bringup.sh doom /dev/ttyUSBx
   ```
   (or `badapple` instead of `doom`). `uart_tsi` is already built and executable. Expect ~5-6 minutes real load time for DOOM (30.7MB ELF, dominated by the embedded 28.8MB WAD) -- that's normal, not a hang. **Watch the FT232RL's own terminal (PuTTY/`screen`/`minicom`) for real console output**: `DG_Init()` calls `uart_puts("[doom] arty100t bare-metal backend up (VGA framebuffer @ 0x04000000)")` over this exact same UART -- seeing that line print is genuine, monitor-independent proof the full chain (bitstream -> boot -> ELF -> real application code) works. Bad Apple prints its own equivalent ("[badapple] loaded, playing").
6. Also physically seat the Pmod VGA onto JB+JC now, even though its output can't be checked yet -- one less thing to fumble with on Sunday.
7. **Sunday, once the monitor arrives**: plug it in and watch. Everything upstream of "does the screen show anything" will already be verified -- this step is just the visual confirmation, not a full bring-up from scratch.

## Known, accepted residual risk (not a blocker)

The timing-closure fix (see the timing violation report) shifted the design's tightest *setup* path elsewhere in the Rocket core's icache/frontend/FPU region, down to **2 picoseconds** of margin -- still officially met (0 failing endpoints), but with essentially zero room left. This has no bearing on tonight's bring-up; it's a note for **future RTL changes only** -- if anything is ever added along that specific path, timing should be re-checked before trusting a new build blind.

## What's already real and doesn't need re-doing
- The address-decode fix, the full-SoC integration test, the CPU regression, coverage, and the real Bad Apple/DOOM/DOOM-gameplay simulation verification from earlier work all still apply -- they tested the underlying TileLink/scan-out logic, which the color upgrade widened but didn't restructure.
- The `usbipd`/Windows bridge setup is confirmed working across many real rebuilds tonight -- no new setup needed, just the sequencing above.
- The real Pmod VGA pinout (JB+JC, 14 pins) is confirmed from two independent sources (Digilent's own reference design, cross-checked against the local `arty-master.xdc`) and is exactly what's baked into the current bitstream's XDC constraints -- verified via the real generated `.shell.xdc` showing all 14 `PACKAGE_PIN`/`IOSTANDARD` entries.

## Board-day session 1 results (2026-09-06): real bring-up attempted, two real open issues found

Physical bring-up was actually attempted tonight -- board powered, bitstream programmed and re-programmed several times, ELF loading proven byte-perfect via self-check, FT232RL wired and rewired. Two genuinely separate problems were found, isolated, and neither is resolved yet. Both are now covered by small, dedicated, easy-to-debug test programs instead of DOOM/Bad Apple (see below) -- debugging a 30MB engine binary made every iteration slow and confounded multiple subsystems at once.

### Issue 1: console UART (FT232RL / Pmod console link) -- silent, cause still unknown
No console output was ever observed, on either JD (pins 3/7, the originally documented mapping) or JA (pins 3/4, a from-scratch rebuild tried as a fallback), in either TX/RX orientation, with a fresh GND wire, and with the FT232RL itself independently proven fully functional via direct self-loopback (TXD wired to RXD on the adapter alone, bytes echoed correctly). Also ruled out via source-level review: the UART0 peripheral's memory address (`0x10020000`) is confirmed identical between `RocketArty100TConfig` and `RocketArty100TVGAConfig`; the base `fesvr` TSI library's load-then-interrupt sequencing has no race condition; `uart.h`'s register bit definitions (`TXFIFO_FULL`, `TXEN`, baud divisor) all match the real SiFive UART0 spec; the "custom boot pin" that looked suspicious turned out to be a simulation-only `PlusArgReader` construct with a fixed, safe default (0) on real hardware, not a floating pin. **What's not yet known: whether the CPU is actually executing any code at all** -- this can't be distinguished from "CPU runs fine, this one peripheral is broken" without either a working JTAG connection to the RISC-V Debug Module (attempted tonight via the FT232RL's hardware bitbang mode over `usbipd`/`vhci_hcd` -- hung in a way that didn't respond even to `SIGTERM`, and a native-Windows OpenOCD attempt was blocked by a Windows UAC elevation step deliberately left for a human to approve) or a working VGA signal (see Issue 2).

### Issue 2: VGA -- monitor reports "no signal"
First-ever real hardware test of the VGA path tonight (previously simulation-only). Pmod seating, VGA cable, and HDMI cable/input were all confirmed connected; the board's own heartbeat LED confirms the bitstream is alive and clocking normally. A clock-rate theory (thinking the ~800x525 VESA-standard timing counter was running at the raw 50MHz bus clock instead of a divided ~25MHz pixel clock) was investigated and **ruled out on closer reading** -- `VGAFramebuffer.scala` already has a `pclk_toggle`/`pixelTick` divide-by-2 gate on the counter, so the real cause of "no signal" is still open. Since HSYNC/VSYNC generation is a free-running hardware counter with zero dependency on CPU execution, a real "no signal" result here (as opposed to a black screen with signal lock) points at either the VGA peripheral's RTL/timing itself or something in the physical chain not yet isolated.

### New: isolated, single-purpose test programs (use these instead of doom/badapple for iterating)
Two new programs exist specifically so each subsystem can be debugged independently, with zero shared code between them (a hang in one can't mask or confound the other):
- **`vga_probe.c`** (`make vga_probe` in `software/doom/baremetal-arty100t`) -- no UART dependency at all. Fills the whole screen solid red -> green -> blue -> white, cycling forever. Load via `uart_tsi +tty=/dev/ttyUSBx vga_probe.elf` (loads in seconds, no self-check needed). See the file's own header comment for what each possible outcome (no signal / locked-but-black / colors cycling) means.
- **`uart_probe.c`** (`make uart_probe`) -- no VGA dependency at all. Prints a boot message, then echoes back every character typed into the FT232RL's terminal -- tests TX and RX as two separate, distinguishable outcomes rather than one combined pass/fail.

### Plan for next session
1. Run `vga_probe.elf` first -- it's the cleanest available signal, since it's fully decoupled from CPU-vs-peripheral ambiguity in a way the UART test isn't (VGA sync doesn't need the CPU; if colors cycle, the CPU is proven to be executing real code).
2. If VGA locks and shows real cycling colors: the CPU is confirmed running, and Issue 1 becomes an isolated console-UART-peripheral bug to chase specifically (not a "is the chip alive" question anymore).
3. If VGA still shows no signal or stays black: prioritize getting a real JTAG connection to the Debug Module (either finish the native-Windows OpenOCD path -- one UAC click needed -- or source an actual JTAG probe) to directly halt the hart and read its PC/registers, since that's the only way left to answer "is it executing" without a working peripheral.
4. Do not resume iterating with `doom`/`badapple` until at least one of the two probes above gives a clean, understood result -- they're too slow to load (5-50 min) and too complex to debug blind.
