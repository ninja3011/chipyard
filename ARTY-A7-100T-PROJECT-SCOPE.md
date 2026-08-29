# DOOM on Arty A7-100T — Project Scope

**Date**: 2026-08-27
**Trigger**: AWS F2 quota request was refused (`CASE_CLOSED`, 8 vCPUs granted vs. 24 needed by even the smallest F2 instance) — this is a parallel, AWS-independent track, not a replacement for the F2 work.

## 1. What's actually already there

Chipyard ships first-class Arty A7-100T support out of the box — this is not a from-scratch port:

- `fpga/src/main/scala/arty100t/` — harness, configs (`RocketArty100TConfig`, `BringupArty100TConfig`), IO binders for UART, UART-TSI, JTAG, and DDR.
- `fpga/fpga-shells/src/main/scala/shell/xilinx/Arty100TShell.scala` — real board overlays: 100MHz sys clock (pin `E3`), SDIO over Pmod JA, QSPI flash, and (via `xilinxarty100tmig`) the DDR3L memory controller.
- `docs/Prototyping/Arty.rst` — a documented, working build+run flow: `make SUB_PROJECT=arty100t bitstream` in Vivado, then `uart_tsi` to load and run a binary over a UART transport.

So the board, the shell, the memory controller, and the build flow are solved problems. What's *not* solved, and is the actual scope of this project, is getting DOOM specifically to run and be watchable on this hardware.

## 2. The hard constraint: no video output, no Ethernet

This is the part worth being upfront about before committing time to it. The Arty100T shell in this repo exposes: UART, UART-TSI, JTAG, SDIO, QSPI flash, DDR3L. **No HDMI/VGA, no Ethernet.** Two consequences:

- **No native display.** There's nowhere to plug in a monitor. Any "watching DOOM" story here needs either (a) a bolted-on Pmod VGA board driven by new RTL we'd have to write, or (b) streaming frames off the board over UART to a viewer on the host — same idea as the netstream architecture already built for F2, just over a much slower link.
- **No Ethernet.** The existing `doomgeneric_netstream.c` / `videoplayer_netstream.c` TCP approach can't be reused as-is — UART is the only link off the board.
- **UART bandwidth is the real bottleneck.** Even at the fastest documented rate (`UART921600RocketArty100TConfig`, 921,600 baud ≈ 90KB/s), a single 320x200 RGBA frame is 256,000 bytes — over 2.8 seconds to transfer *one frame*, before any decode overhead. Full-motion video over this link is not realistic; even the 1-bit-packed Bad Apple format (8KB/frame) tops out around 11 fps of pure transfer time, before any framebuffer/paint overhead on the viewer side.

## 3. What LUT budget actually allows

Real numbers from the F2 build for comparison: BOOM + uncore used ~15.6% of the VU47P's ~2.85M LUTs — roughly 445,000 LUTs. The entire Artix-7 100T chip on the Arty has about **101,000 LUTs total**. BOOM does not fit here, period — it's over 4x the whole chip's capacity.

`RocketArty100TConfig` uses a small in-order Rocket core instead, which is the config this project would build on. One thing working in our favor: both `doombom-doom-final.elf` and `doombom-video-final.elf` are already built **soft-float** (no hardware F/D extension needed) — confirmed via `file`, both show `soft-float ABI`. That's a smaller, cheaper core config than the RV64IMAFD originally scoped in `CLAUDE.md` for the BOOM target, and it's a good match for what actually fits on this chip.

## 4. Two honest tiers

### Tier 1 — Bare-metal DOOM, matches the documented flow (recommended starting point)

- Build `RocketArty100TConfig`, confirm it boots and `uart_tsi` can load and run a small test binary (`dhrystone.riscv`) — this is the existing, documented, low-risk path.
- Port `doomgeneric` to run **bare-metal** (no Linux, no initramfs) directly against Rocket's memory map — a custom low-level platform layer instead of Linux syscalls, similar in spirit to embedded DOOM ports for microcontrollers.
- Load the compiled bare-metal ELF (expected: low tens of MB at most, not the 140MB FireMarshal Linux image) into DRAM over `uart_tsi`, the same mechanism already used for `dhrystone.riscv`.
- Video out: pick one—
  - **(1a) Periodic snapshot dump** over UART — not live playback, but a genuine "here's the CPU actually rendering DOOM" proof, viewable frame-by-frame.
  - **(1b) Add a Pmod VGA board** (~$20–30, e.g. Digilent Pmod VGA) and write a small memory-mapped framebuffer + VGA timing generator in Chisel, wired in as a new IO binder alongside the existing UART/JTAG ones. This is genuinely new RTL work, but bounded in scope — a VGA timing generator is a well-understood, well-documented piece of hardware.
- **This tier is realistic to scope in days, not weeks**, and (1b) gets you an actual monitor showing actual real-time DOOM on real silicon you own.

### Tier 1c — Actually playable: 1b + keyboard input over the existing UART console

Video alone (1b) is a watch-only demo. Input turns out to already be fully solved, and more simply than it first looks: the bandwidth wall that rules out streaming *video* over UART (a frame is hundreds of KB) doesn't apply to *input* — a keypress is 1 byte. `RocketArty100TConfig` already wires up a real UART console (`WithArty100TUART`/`WithArty100TPMODUART`), separate from the `uart_tsi` loading channel. Polling that UART's RX FIFO for WASD/fire keypresses sent from a terminal on the host needs **zero new hardware and zero new pin binding** — it's a change to the bare-metal DOOM port's input-polling function, nothing else.

This beats onboard buttons on every axis that matters here: a full keyboard's worth of keys instead of 4 discrete buttons, and it reuses infrastructure that's already there instead of adding a new GPIO overlay binding. (The board's 4 onboard pushbuttons/switches — `ButtonArtyPlacedOverlay`/`SwitchArtyPlacedOverlay` in `Arty100TShell.scala`, wired the same way the LEDs already are in `Harness.scala` — remain available later as a physical-control novelty, but they're not needed for a genuinely playable build.) Full mouse-look would still need a USB host or Pmod PS/2, neither of which exists in this shell today.

### Tier 2 — Full Linux + video-netstream parity with the AWS build (stretch goal)

- Boot real Linux from the SD card via the existing `SDIOArtyPlacedOverlay`, reusing the FireMarshal-built root filesystem approach from the F2 work.
- This needs an SD card block driver in the boot flow and a Linux SD/MMC driver wired through — **not currently solved by anything in this repo**; it's genuine new board-bring-up software work, comparable in kind (though smaller in scale) to what the F2 AGFI build required.
- Video would still need Tier 1's VGA answer, or a USB/SPI Ethernet adapter (not currently wired into the shell) to bring back real netstream-over-TCP parity with the DOOM/Bad Apple pipeline already built.
- **This is realistically its own multi-week side project**, not a quick follow-on to Tier 1 — flagging that honestly rather than scoping it as "day 2 of the Arty work."

## 5. Bill of materials (if not already owned)

| Item | Est. cost | Needed for |
|---|---|---|
| Arty A7-100T | ~$150 (board itself; you may already have one per earlier Basys3 mention — confirm before buying) | both tiers |
| Pmod VGA | ~$20–30 | Tier 1b, Tier 2 |
| USB-A to Micro-USB cable | usually included | UART/JTAG/programming |
| microSD card | ~$10 | Tier 2 only |

## 6. Suggested task breakdown (Tier 1 only, the realistic near-term scope)

1. Build and flash `RocketArty100TConfig`, confirm `uart_tsi` round-trip with `dhrystone.riscv`. (validates the whole toolchain end-to-end before touching DOOM)
2. Strip `doomgeneric`'s platform layer down to bare-metal: replace file I/O (WAD loading) with a linked-in resource, replace the OS-backed timer/input with Rocket's mtime CSR and a minimal UART input path.
3. Get it compiling and linking against Rocket's bare-metal memory map; confirm it boots to first frame under `uart_tsi`.
4. Decide 1a vs. 1b/1c for video; implement whichever is chosen.
5. If 1b/1c: write the VGA timing-generator Chisel module, wire it as a new `HarnessBinder`/`IOBinder` pair (mirroring the pattern already used for `WithArty100TUART`), get a real image on a real monitor.
6. If 1c: change DOOM's input-polling function to read keypress bytes off the existing UART console RX FIFO instead of a keyboard device — no new hardware or pin binding required.

## 7. Bottom line

This is a genuinely good parallel track while the F2 quota situation gets sorted out — real hardware, zero cloud dependency, and Chipyard already has 90% of the plumbing built. The one thing worth being clear-eyed about going in: this will not look like the AWS build (full Linux, full-res streamed video over the network). It'll be a smaller, in-order core running a bare-metal DOOM port, and the "watch it play" story is either periodic frame dumps or a VGA add-on board — not a drop-in reuse of the existing netstream pipeline.
