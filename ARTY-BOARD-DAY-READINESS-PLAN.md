# Arty A7-100T board day: readiness plan

**Written**: 2026-09-02, ahead of remaining hardware (Pmod VGA, FT232RL, jumpers) arriving Friday.
**Goal**: walk in, plug in, hit play -- with the real gaps closed *before* that day, not discovered on it.

## The two real blockers, closed before Friday, not on it

Tonight's session upgraded the VGA peripheral from monochrome to real 12-bit color and verified the RTL in simulation. Two real consequences of that change are **not yet done**, and both need real work, not just wiring:

1. **No bitstream exists for the color RTL yet.** The last real, clean-timing, `.bit` file built (`fpga/generated-src/.../RocketArty100TVGAConfig/obj/Arty100THarness.bit`) is from *before* tonight's color upgrade -- it still expects a 1-bit `vga_video` signal on JA, not real R/G/B on JB+JC. Programming the board with that bitstream and wiring up a real Pmod VGA per the new pin mapping would not work (wrong peripheral, wrong pins). **A fresh Vivado build against the current RTL is required.**
2. **DOOM and Bad Apple's software still write 1-bit data.** `doomgeneric_arty100t.c`'s `DG_DrawFrame` and `badapple_arty100t.c` both currently threshold their source pixels down to a single on/off bit and pack 8 pixels/byte -- the *old* framebuffer layout. The peripheral's real memory layout is now 16 bits/pixel (`R<<8 | G<<4 | B`), 153,600 bytes total, not 9,600. Loading the *current* ELFs onto a real color-upgraded bitstream would write into the wrong byte layout entirely -- **the software needs updating to write real color pixels in the new format, and both ELFs need rebuilding.**

Recommend closing both before Friday, in this order: **(1) software update + rebuild first** (fast, no Vivado wait), **(2) full Vivado rebuild second** (the multi-hour part) -- so the final bitstream and the final ELFs are built from the *same*, already-correct RTL+software pairing, not patched together at the last minute.

## Step-by-step: closing the two blockers

### 1. Update DOOM's `DG_DrawFrame` for real color
- Change the framebuffer stride/addressing from 1 bit/pixel (`FB_BYTES_PER_ROW = FB_WIDTH/8`) to 16 bits/pixel (`FB_WIDTH*2` bytes/row).
- Replace `sampleThreshold` (blue-channel >=128 -> 1 bit) with a real 4-bit-per-channel downsample of the real BGRA pixel: `r4 = (px>>20)&0xF`, `g4 = (px>>12)&0xF`, `b4 = (px>>4)&0xF` (adjust shifts to match doomgeneric's actual byte order, already confirmed BGRA tonight), then write `(r4<<8)|(g4<<4)|b4` as a 16-bit word per pixel.
- Keep the existing 20-row letterbox centering math -- geometry is unchanged, only pixel encoding changes.

### 2. Update Bad Apple's player for real color
- `badapple_arty100t.c` currently does a direct byte-for-byte copy from the `.vidf`'s packed 1bpp rows. With color, each source bit (on/off) should map to a real color choice -- simplest faithful option: on -> full white (`0xF, 0xF, 0xF`), off -> full black (`0,0,0`), written as 16-bit words instead of copied bytes. Keeps Bad Apple's real visual identity (still black/white) while exercising the real color write path end to end.
- Same 20-row letterbox, same row-major order -- only the per-pixel encoding and the row byte-stride change (40 bytes/row -> 640 bytes/row).

### 3. Rebuild both ELFs
- `cd software/doom/baremetal-arty100t && make doom && make badapple` (or the project's real equivalent targets) -- confirm both link clean against the updated color-writing code.

### 4. Rebuild the real bitstream
- Re-elaborate `RocketArty100TVGAConfig` from `fpga/` (`make SUB_PROJECT=arty100t CONFIG=RocketArty100TVGAConfig verilog`), confirm the DTS shows the framebuffer's real (now larger) address region as a single contiguous range -- the same address-decode check that caught last time's real bug.
- Mirror the fresh `generated-src` to the Windows build directory, rewrite absolute WSL paths to Windows paths in the `.f` manifests (the real, previously-diagnosed step -- generate `.vsrcs.f` via `make ... bitstream` from `fpga/`, not `verilog` alone, since only `bitstream` actually produces that file for real).
- Run the real Vivado flow via the Windows bridge (`vivado.tcl`, absolute paths for `-F`/`-ip-vivado-tcls` this time -- confirmed necessary tonight). Expect a similar real timing result to before (~WNS +0.2 to +0.5ns range) given the color peripheral only adds ~640 LUTs worth of logic on top of the memory footprint change.
- Copy the resulting `.bit` back to the WSL side, confirm timing report shows "All user specified timing constraints are met" before trusting it.

## Physical bring-up checklist, board day

### Hardware needed (confirm all present before starting)
- [ ] Arty A7-100T board (have)
- [ ] Pmod VGA module (arriving Friday)
- [ ] FT232RL USB-to-serial adapter + 3 jumper wires, TX/RX/GND (arriving Friday)
- [ ] VGA cable (have)
- [ ] VGA-to-HDMI converter (have)
- [ ] HDMI monitor (have)
- [ ] USB-Micro cable, board-to-PC (confirm have -- this is the one link doing double duty for programming *and* TSI loading)

### Physical wiring
- [ ] **Pmod VGA onto JB *and* JC together** (not JA -- that was the old monochrome mapping; color needs the full 14-pin real mapping across both headers, confirmed from Digilent's own reference design tonight).
- [ ] VGA cable: Pmod VGA -> VGA-to-HDMI converter -> HDMI monitor.
- [ ] FT232RL wired to **Pmod JD pins 3 and 7** (TX/RX) + GND -- this is the *separate* console link DOOM's keyboard input reads from; the board's onboard USB only carries the TSI load link, not this.
- [ ] FT232RL's own USB side plugged into the PC, ready for its own `usbipd attach` once needed.

### Sequencing (do in this order -- established real reason each step needs to happen in this order)
1. Plug the Arty board's USB-Micro cable in. Confirm it enumerates on **Windows** (Device Manager).
2. **Program the bitstream from Windows** (Vivado Hardware Manager needs to see the board directly for this step): `vivado -mode batch -source software/doom/baremetal-arty100t/program_bitstream.tcl`.
3. Once programmed, route the board into **WSL**: `usbipd list` (find its bus ID) -> `usbipd bind --busid <id>` (elevated PowerShell, one-time) -> `usbipd attach --wsl --busid <id>`.
4. Plug in the FT232RL, route it into WSL too (separate `usbipd attach` for its own device).
5. Load and run: `cd software/doom/baremetal-arty100t && ./bringup.sh doom <tsi-tty-device>` (or `badapple` instead of `doom`). Expect ~5-6 minutes for DOOM's WAD-embedded ELF to load over `uart_tsi` at its documented rate -- this is real and expected, not a hang.
6. Watch the monitor. If nothing appears: check the Pmod VGA seated fully on *both* JB and JC (a half-seated double-Pmod module is a classic real failure mode), and confirm the FT232RL's TX/RX aren't swapped (a common wiring mistake, harmless to swap and retry).

## What's already real and doesn't need re-doing
- The address-decode fix, the full-SoC integration test, the CPU regression, coverage, and the real Bad Apple/DOOM/DOOM-gameplay simulation verification from earlier tonight all still apply -- they tested the underlying TileLink/scan-out logic, which the color upgrade didn't change structurally, only widened.
- The `usbipd`/Windows bridge setup itself is already confirmed working from tonight's Vivado rebuilds -- no new setup needed there, just the sequencing above.
