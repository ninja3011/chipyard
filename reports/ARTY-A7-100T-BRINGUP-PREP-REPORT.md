# Arty A7-100T Bring-Up: Prep Day Report

**Date**: 2026-08-28
**Trigger**: AWS F2 quota request came back refused (`CASE_CLOSED`, 8 vCPUs granted vs. 24 needed by even the smallest F2 instance). Rather than wait on a second quota appeal, today's work opened a second, AWS-independent hardware track: a Digilent Arty A7-100T, bought today, still in transit.

## 1. The goal for today

With no board in hand yet, the goal was to get everything that *doesn't* need the physical hardware fully done, so the moment it arrives there's zero dead time: environment bridges, a real elaborated hardware config, and a real (not sketched) bare-metal DOOM build ready to load.

## 2. Bridging WSL and Windows

This machine's Vivado install turned out to live entirely on the Windows side (`C:\AMDDesignTools\2026.1\Vivado`), not WSL — meaning bitstream synthesis and board programming both need to cross that boundary. Set up `usbipd-win` (installed via an elevated Windows PowerShell) paired with `usbip` client tools inside WSL (`linux-tools-generic`, `hwdata`), confirmed both sides working (`usbipd-win` 5.3.0 responds, WSL's `usbip version` responds). Once the board is plugged in, `usbipd bind` (one-time, needs elevation) then `usbipd attach --wsl` will route its USB-JTAG/UART into this WSL session directly.

## 3. Two real environment bugs, found and fixed

Neither of these were config problems — both were pre-existing, silent breakage in the local toolchain that would have blocked *any* Chipyard elaboration, not just this one:

- **Wrong JDK.** The shell was resolving `java` to the system's JDK 21 instead of the conda environment's bundled JDK 20. This old SBT/Scala 2.12.17 toolchain doesn't tolerate JDK 21's classfile format — it crashed deep inside the Scala compiler's own classfile parser trying to read a core JDK class, surfacing as `NoClassDefFoundError: Could not initialize class sbt.internal.parser.SbtParser$`. Initially misdiagnosed as a corrupted `scala-library.jar` (it wasn't — verified byte-identical, valid ZIP, both before and after a cache-clear); the real fix was forcing `JAVA_HOME` to the conda environment's JDK before invoking `make`.
- **Missing `dtc`.** Elaboration failed a second time with `Failed to run dtc; is it in your path?` — the device tree compiler wasn't installed as a system package (a copy existed inside the conda environment but wasn't reliably on `PATH` in this shell context). Fixed with `apt install device-tree-compiler`.

Once both were fixed, `RocketArty100TConfig` elaborated cleanly (288 seconds).

## 4. Real facts confirmed about this exact hardware config

Elaboration output ended up being the single most useful artifact of the day — it turned several "TODO, confirm on hardware" placeholders from yesterday's scope doc into real numbers:

| Fact | Value | Source |
|---|---|---|
| Base clock | 50MHz | `chisel.log`, confirms `WithArty100TTweaks` default |
| UART0 base address | `0x10020000` | Generated address map + DTS (`sifive,uart0`) |
| UART clock | 50MHz (`pbus_clock`) | DTS |
| DRAM | `0x80000000`, 256MB | Generated address map |

All of the Verilog, the Vivado IP TCL scripts (`arty100tmig.vivado.tcl`, `shell.vivado.tcl`, `harnessSysPLLNode.vivado.tcl`), and the synthesis file list are generated and sitting on disk, ready for Windows Vivado to consume — see `ARTY-A7-100T-WINDOWS-HANDOFF.md` for the exact command.

## 5. A real, linked bare-metal DOOM build — not a sketch

Built the bare-metal `doomgeneric` port scoped in yesterday's plan, on the toolchain's real newlib + `nano.specs`, rather than hand-rolling a libc:

- **`uart.h`** — real SiFive UART0 driver using the confirmed `0x10020000` base address and a computed 115200-baud divisor for the confirmed 50MHz clock.
- **`syscalls.c`** — newlib syscall layer providing `_write`/`_read` (routed to the UART), `_open`/`_lseek`/`_close`/`_fstat` (special-cased to serve the WAD from an embedded blob, fail cleanly for anything else), `_sbrk` (bump allocator bounded against DRAM's top, leaving an 8MB stack reserve), plus the handful of trivial stubs (`_exit`, `_kill`, `_getpid`, `_isatty`, `mkdir`) newlib needs satisfied to link at all.
- **`wad_embed.S`** — links Freedoom Phase 1 (28.8MB) directly into the ELF via `.incbin`, since there's no filesystem.
- **`doomgeneric_arty100t.c`** — the six functions doomgeneric requires per platform. Timing uses the RISC-V `rdtime` CSR (portable, no board-specific address needed). Input polls the confirmed real UART for single-byte WASD/fire/enter/escape keypresses. Video output is the one honest placeholder in the whole build — `DG_DrawFrame` is wired up but writes nowhere yet, because the VGA framebuffer peripheral this needs doesn't exist in the design (that's separate Chisel work, scoped as Tier 1b in `ARTY-A7-100T-PROJECT-SCOPE.md`).

**Three real bugs surfaced and were fixed getting this to link**, each a legitimate "first time building this exact thing" issue rather than a typo:
1. A stale x86 `.o` file sitting inside `doomgeneric`'s own source tree (leftover from the Bad Apple video work) collided with a Makefile `VPATH` lookup, silently tricking Make into believing our bare-metal targets were already built. Fixed by switching from `VPATH` search to explicit source paths in the Makefile.
2. Dropped `dummy.c` while trimming the engine's source list to remove networking — that file turned out to be the one providing the stub globals (`drone`, `net_client_connected`) non-networked platforms need. Added back.
3. `mkdir` was referenced by the savegame-directory setup code with no implementation in this filesystem-less environment. Added a no-op stub.

**Result**: `doom-arty100t.elf`, 30.7MB (dominated by the embedded WAD), RV64 RVC double-float ABI, entry point `0x80000000`, links clean against the confirmed real hardware config. Ready to load via `uart_tsi` the moment the board and its UART are reachable.

## 6. Known cost going into first bring-up

Loading a 30.7MB ELF over `uart_tsi` at its fastest documented rate (~90KB/s) will take on the order of 5–6 minutes per attempt, since the whole WAD travels with it every time. Worth expecting on the first few iterations, not a bug to chase once it shows up.

## 7. What's left

- Windows-side Vivado synthesis (command ready, doesn't need the board — could run tonight).
- Board arrival, then: `usbipd bind`/`attach`, program the bitstream, load `doom-arty100t.elf` via `uart_tsi`, confirm the console boots and UART keypresses register.
- The VGA framebuffer peripheral (Tier 1b) — the one piece of this whole build that's still a documented placeholder rather than working code.
