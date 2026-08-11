# Project Report: RISC-V "Hello World" on a Basys3 FPGA (No JTAG Probe)

**Project:** Bring up a Chipyard-generated RISC-V (Rocket-chip) SoC on a Digilent Basys3 board, load and run a bare-metal "Hello World" program, using only the board's own switches/buttons/LEDs and its stock microUSB cable — no JTAG probe, no external wiring.

**Repository root:** `/home/ninadjangle/chipyard`
**All custom project files:** `ninad/` and `ninad/basys3/`

---

## 1. Objective and Constraints

The goal was to select, build, and physically deploy a RISC-V SoC config from Chipyard onto a Basys3 (Xilinx Artix-7 `xc7a35tcpg236-1`) board such that it prints `Hello World from TinyRocket!` over UART.

Constraints that shaped every decision below:
- Basys3 has **no external DRAM** — only ~225KB of on-chip Block RAM.
- No JTAG probe was available, and no jumper wires were available at all, at any point.
- Vivado runs on Windows, outside the WSL/Linux environment used for Chipyard/Chisel/Verilator work — all hardware-side steps (synthesis, programming, PuTTY) happen on a separate machine/OS.

These constraints ruled out the "normal" chipyard FPGA flow (JTAG + OpenOCD + gdb) entirely and forced a from-scratch design: load the program using only on-board switches and buttons.

---

## 2. Configuration Selection

### 2.1 Why `TinyRocketConfig` as a starting point

File: `generators/chipyard/src/main/scala/config/RocketConfigs.scala`

Of all the configs in that file, `TinyRocketConfig` is the only one that removes the L2 cache and off-chip memory port and runs the Rocket core entirely out of its own **DTIM** (Data Tightly-Integrated Memory — a small block of memory built into the core itself). This matters because Basys3 has no DDR memory chip at all, so any config that assumes an off-chip DRAM channel (i.e. every other config in that file) is a non-starter on this board. The DTIM is plain on-chip BRAM, inferred automatically by Vivado from the generated Verilog — no memory controller IP required.

### 2.2 Why `TinyRocketDMIConfig` (final config)

The normal way to get a program into a chip like this is JTAG: an external USB-JTAG probe, a debugger (OpenOCD/gdb), and the RISC-V Debug Module. We had no probe and no wires to attach one even if we'd had it. Chipyard's debug module can instead expose its control interface as **DMI** (Debug Module Interface) — a plain, parallel request/response bus (7-bit address, 32-bit data, 2-bit op-code, valid/ready handshakes) instead of a bit-serial JTAG protocol. Critically, DMI can be driven by **on-chip logic**, not just an external probe — which meant we could build our own tiny "debugger" out of Verilog, living inside the FPGA itself, triggered by a single button press.

This required two config fragments, added to `RocketConfigs.scala`:

```scala
// Enables the Debug Module's System Bus Access (SBA) registers, so memory
// can be written directly over DMI without halting the hart or using the
// abstract-command/program-buffer path. Off by default (rocket-chip
// DebugModuleParams.hasBusMaster = false).
class WithSBADebugModule extends Config((site, here, up) => {
  case freechips.rocketchip.devices.debug.DebugModuleKey =>
    up(freechips.rocketchip.devices.debug.DebugModuleKey).map(_.copy(hasBusMaster = true))
})

// Same as TinyRocketConfig, but exposes the Debug Module over DMI instead
// of JTAG, and enables SBA so an on-board sequencer can write firmware
// directly into the DTIM with no external hardware at all.
class TinyRocketDMIConfig extends Config(
  new testchipip.soc.WithNoScratchpads ++
  new freechips.rocketchip.subsystem.WithIncoherentBusTopology ++
  new freechips.rocketchip.subsystem.WithNBanks(0) ++
  new freechips.rocketchip.subsystem.WithNoMemPort ++
  new chipyard.config.WithDMIDTM ++
  new WithSBADebugModule ++
  new freechips.rocketchip.rocket.With1TinyCore ++
  new chipyard.config.AbstractConfig)
```

`chipyard.config.WithDMIDTM` is a pre-existing chipyard fragment (`generators/chipyard/src/main/scala/config/fragments/PeripheralFragments.scala`) that switches `ExportDebug.protocols` from `Set(JTAG)` to `Set(DMI)`. `WithSBADebugModule` is new, written for this project, and turns on **System Bus Access** — a feature of the RISC-V debug module that lets a debugger write directly to any memory-mapped address without halting the CPU or using the more complex abstract-command/GPR path.

### 2.3 A config bug found and fixed along the way

The original (pre-project) `TinyRocketConfig` had two lines added by a previous attempt at exposing an AXI4 external memory port:

```scala
new freechips.rocketchip.subsystem.WithExtMemSize((1 << 20) * 1L) ++
new chipyard.iobinders.WithAXI4MemPunchthrough ++
```

These were **dead code**: in Chipyard's `Config` composition, `++` behaves like `orElse`, and the leftmost fragment wins on key conflicts. `WithNoMemPort` sat to the left of these two lines and unconditionally set `ExtMem => None`, so the two lines never had any effect (confirmed by inspecting the generated `ChipTop.sv`, which had no `axi4_mem_*` ports). They were removed, since the DTIM-only approach needs no external memory port at all.

---

## 3. Generating the Verilog

All Scala/Chisel/FIRRTL work happens inside the WSL/Linux Chipyard checkout. The build system needs a specific Conda environment activated (`env.sh`) — using the system-default `java`/`sbt` instead causes a JDK-compatibility crash in the Scala 2.12 compiler.

```bash
source /home/ninadjangle/chipyard/env.sh
cd /home/ninadjangle/chipyard/sims/verilator
make verilog CONFIG=TinyRocketDMIConfig
```

This produces, among many files, the real synthesizable design at:
```
sims/verilator/generated-src/chipyard.harness.TestHarness.TinyRocketDMIConfig/gen-collateral/ChipTop.sv
sims/verilator/generated-src/chipyard.harness.TestHarness.TinyRocketDMIConfig/gen-collateral/DigitalTop.sv
```

### 3.1 Staging a clean Vivado source set

Chipyard's build also emits hundreds of simulation-only files (`TestHarness.sv`, `SimJTAG.v`, `SimUART.v`, etc.) mixed in with the real design files. The exact list of *only* the real, synthesizable design files is given by the `.top.f` manifest:

```bash
cd sims/verilator/generated-src/chipyard.harness.TestHarness.TinyRocketDMIConfig
sed 's#.*/gen-collateral/##' chipyard.harness.TestHarness.TinyRocketDMIConfig.top.f > /tmp/top_names_dmi.txt

DEST=/home/ninadjangle/chipyard/ninad/basys3/vivado_sources
rm -rf "$DEST" && mkdir -p "$DEST"
cd gen-collateral
while read -r f; do cp "$f" "$DEST/"; done < /tmp/top_names_dmi.txt

# One extra file lives outside the .top.f manifest: the macro-compiler
# output that gives the actual body of the DTIM's memory-macro blackboxes
# (rockettile_dcache_data_arrays_0_ext, rockettile_icache_tag_array_0_ext,
# rockettile_icache_data_arrays_0_0_ext). Without it, Vivado synthesis
# fails with "module not found" for these three modules.
cp chipyard.harness.TestHarness.TinyRocketDMIConfig.top.mems.v "$DEST/"
```

Result: **`ninad/basys3/vivado_sources/`** — 265 files, the complete and exact synthesizable design, ready to import into Vivado.

---

## 4. The Firmware

**Files:** `ninad/main.c`, `ninad/link.ld`, `ninad/boot.S`

### 4.1 `main.c`

```c
// SiFive UART0 (chipyard.config.WithUART), instantiated at 0x10020000 in
// TinyRocketConfig -- confirmed from the generated .dts ("serial@10020000")
// and the regmap json for that address.
#define UART_BASE       0x10020000UL
#define UART_TXDATA     (*(volatile unsigned int *)(UART_BASE + 0x00)) // [7:0] data, [31] full
#define UART_TXCTRL     (*(volatile unsigned int *)(UART_BASE + 0x08)) // [0] txen, [1] nstop
#define UART_DIV        (*(volatile unsigned int *)(UART_BASE + 0x18)) // [15:0] baud divisor

#define SYS_CLK_HZ      10000000UL
#define UART_BAUD       115200UL

static void uart_init(void) {
    UART_DIV = (SYS_CLK_HZ / UART_BAUD) - 1;
    UART_TXCTRL = 0x1; // txen = 1 (resets to 0 -- TX is disabled until this is set)
}

static void uart_putc(char c) {
    while (UART_TXDATA & (1u << 31)) { } // spin while TX FIFO full
    UART_TXDATA = (unsigned char)c;
}

static void print_str(const char *str) {
    while (*str) uart_putc(*str++);
}

int main() {
    uart_init();
    print_str("Hello World from TinyRocket!\n");
    return 0;
}
```

Two firmware bugs found and fixed from an earlier draft: (1) the UART base address was originally hardcoded to `0x64000000`, which is wrong for this specific config (verified against the generated `.dts`); (2) `UART_TXCTRL.txen` resets to `0` — the UART was never actually enabled before the first write.

### 4.2 `link.ld`

```
MEMORY {
  /* TinyRocketConfig's DTIM (Rocket TCM) is 16KB, per the generated
     .dts: dtim@80000000 { reg = <0x80000000 0x4000>; } */
  BRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16K
}
```

(An earlier draft assumed 64KB — the real DTIM is 16KB, confirmed from the generated device tree.)

### 4.3 `boot.S`

```asm
.section .text.init
.global _start
_start:
    li sp, 0x80004000      /* top of the 16KB DTIM */
    call main
loop:
    j loop
```

### 4.4 Compiling

```bash
export PATH=/home/ninadjangle/chipyard/.conda-env/riscv-tools/bin:$PATH
cd /home/ninadjangle/chipyard/ninad
riscv64-unknown-elf-gcc -march=rv32imac -mabi=ilp32 -static -mcmodel=medany \
  -nostdlib -nostartfiles -fno-common -O2 -Wall -T link.ld boot.S main.c -o firmware.elf
riscv64-unknown-elf-objcopy -O verilog firmware.elf firmware.mem
```

Output: **`ninad/firmware.elf`**, **`ninad/firmware.mem`** — 94 bytes of real program, entry point `0x80000000`.

---

## 5. The Vivado-Side Design

### 5.1 `ninad/basys3/BasysTop.v` — top-level wrapper

Instantiates:
- A **Clocking Wizard** IP (`clk_wiz_0`, added via Vivado's IP Catalog, not hand-written) — 100MHz board oscillator in, 10MHz system clock out. 10MHz was chosen deliberately low to leave generous timing margin on a small, low-speed-grade FPGA.
- A reset synchronizer (BTNC or MMCM-not-locked → clean synchronous reset).
- A start-button synchronizer for `BTNU`, gated by `SW0` (an "arm" safety interlock).
- **`DmiAutoloader`** (see 5.2).
- **`ChipTop`** (the generated design), with:
  - `uart_0_txd`/`uart_0_rxd` → Basys3's `RsTx`/`RsRx` (USB-UART, same cable as programming)
  - `dmi_dmi_*` ports → `DmiAutoloader`
  - `dmi_dmiClock`/`dmi_dmiReset` → the same `sys_clk`/`sys_reset` as everything else
  - `serial_tl_0_*` tied off (simulation-only interface, unused on real hardware)
  - `custom_boot` tied to 0 (not needed — see 5.2)

### 5.2 `ninad/basys3/DmiAutoloader.v` — the on-chip "debugger"

This is the core piece of original engineering in this project: a hardware state machine that replays a fixed sequence of DMI transactions — playing the same role an external JTAG probe + OpenOCD + gdb would normally play — triggered by a single button press.

**Register bit positions used** (`dmcontrol`=0x10, `sbcs`=0x38, `sbaddress0`=0x39, `sbdata0`=0x3c) were not guessed: address values and field *declaration order* came from `generators/rocket-chip/src/main/scala/devices/debug/dm_registers.scala`, and the exact numeric bit offsets were cross-checked against OpenOCD's own `src/target/riscv/debug_defines.h` (fetched directly from `openocd-org/openocd` on GitHub).

The transaction sequence (final, working version):

1. `dmcontrol.dmactive = 1` — activate the debug module
2. `dmcontrol.dmactive=1, ndmreset=1` — hold the hart in reset
3. `sbcs` config: `sbaccess=2` (32-bit), `sbautoincrement=1`
4. `sbaddress0 = 0x80000000` (DTIM base)
5. 24 × `sbdata0` writes — the compiled firmware, word by word (each address auto-increments by 4)
6. `sbaddress0 = 0x02000000` (CLINT's `msip` register for hart 0)
7. `sbdata0 = 1` — arm a pending software interrupt (**while still in reset** — see debugging section)
8. `dmcontrol.dmactive=1, ndmreset=0` — release reset; the hart boots, takes the pending interrupt, and jumps to the freshly-written program

The FSM issues each write, then **reads `sbcs` back and polls its `sbbusy` bit until clear**, and **retries any transaction whose DMI response indicates failure**, before moving to the next one — both additions were the result of direct hardware/simulation debugging (Section 7).

### 5.3 `ninad/basys3/basys3.xdc` — pin constraints

Pin locations for `CLK100MHZ`, `BTNC`, `BTNU`, `SW0`, `LED0-2`, `RSTX`, `RSRX` were copied verbatim from Digilent's official master constraints file, fetched directly rather than recalled from memory:

```bash
curl -sL "https://raw.githubusercontent.com/Digilent/digilent-xdc/master/Basys-3-Master.xdc" \
  -o Basys3-Master.xdc
```

Also includes: a `create_clock` for the 100MHz input, and `set_false_path` for the three asynchronous buttons/switch (they only feed synchronizer inputs, not real timing paths).

### 5.4 `ninad/basys3/vivado_sources/EICG_wrapper.v` — a hand-patched file

`EICG_wrapper` is an ASIC-style, latch-based clock-gating cell that rocket-chip's debug module infrastructure inserts into the clock tree (used to gate the debug domain's clock based on `dmactive`). Implemented as real FPGA fabric logic (a LUT + latch), it caused **73 hold-time violations** during implementation (`report_methodology`: *"TIMING-14: LUT on the clock tree"*). Since `dmactive` is asserted almost immediately by `DmiAutoloader` and never deasserts again during normal operation, no real power-gating behavior is lost by making this a plain pass-through:

```verilog
module EICG_wrapper(
  output out,
  input en,
  input test_en,
  input in
);
  assign out = in;
endmodule
```

This file lives only in `vivado_sources/` (a staged copy), not in the Chisel source tree — if `vivado_sources/` is ever regenerated from a fresh `make verilog` run, this edit must be re-applied.

---

## 6. Vivado Build Steps

1. Create a new Vivado project, part `xc7a35tcpg236-1` (verified against Digilent's own board files, not the generic `xc7a35t`).
2. Add all files from `ninad/basys3/vivado_sources/` as design sources.
3. Add `ninad/basys3/BasysTop.v` and `ninad/basys3/DmiAutoloader.v` as design sources; set **`BasysTop`** as the top module (not `ChipTop`).
4. Add `ninad/basys3/basys3.xdc` as a constraints source.
5. Add the **Clocking Wizard** IP, named exactly `clk_wiz_0`: primary input 100.000MHz single-ended; output `clk_out1` = 10.000MHz; Reset Type Active High; default port names (`clk_in1`, `clk_out1`, `reset`, `locked`).
6. Project Settings → General → Verilog Options → add the **`SYNTHESIS`** macro define. Without it, `plusarg_reader.v` (used for inert TileLink-monitor watchdog logic) tries to use a non-synthesizable simulation-only construct.
7. Run Synthesis → Implementation → Generate Bitstream.
8. Hardware Manager → Open Target → Auto Connect → Program Device.

---

## 7. Debugging Journey (Simulation-Verified Bug Fixes)

Because no JTAG probe was available to inspect what was happening on real hardware, every one of the following bugs was root-caused using **direct Verilator simulation** of the exact `ChipTop` + `DmiAutoloader` combination — not guessed and not fixed blind. A minimal testbench, `ninad/basys3/tb_dmi.v`, instantiates both modules directly (bypassing the Xilinx-only Clocking Wizard) and hierarchically probes internal signals for diagnosis. It was compiled directly with Verilator, independent of the full Chipyard Makefile flow, for fast iteration:

```bash
source /home/ninadjangle/chipyard/env.sh
mkdir -p /tmp/dmi_sim && cd /tmp/dmi_sim
verilator --binary --timing -Wno-fatal -Wno-WIDTH -Wno-CASEINCOMPLETE --top-module tb_dmi \
  -y /home/ninadjangle/chipyard/ninad/basys3/vivado_sources \
  /home/ninadjangle/chipyard/ninad/basys3/vivado_sources/chipyard.harness.TestHarness.TinyRocketDMIConfig.top.mems.v \
  /home/ninadjangle/chipyard/ninad/basys3/DmiAutoloader.v \
  /home/ninadjangle/chipyard/ninad/basys3/tb_dmi.v \
  -o tb_dmi.exe
./obj_dir/tb_dmi.exe
```

**Bug 1 — Missed response pulse.** The FSM only started "listening" for a DMI response after transitioning into a dedicated wait state; simulation showed the debug module can return a response in the *same cycle* the request handshake completes, before the FSM had moved states. Fixed by latching any response the instant it fires, independent of current state (`resp_latched`/`resp_fire` in `DmiAutoloader.v`).

**Bug 2 — No wake-up mechanism.** After releasing `ndmreset`, the hart correctly reboots and runs the boot ROM (`generators/testchipip/src/main/resources/testchipip/bootrom/bootrom.S`) — but that ROM's design is to execute `wfi` (wait-for-interrupt) and stay parked there until something sends it an interrupt. Fixed by writing `1` to CLINT's `msip` register for hart 0 (address `0x02000000`, confirmed via `CLINTConsts.msipOffset(0) = 0`), the same mechanism `testchipip/boot/CustomBootPin.scala` uses.

**Bug 3 — SBA writes silently ignored once the hart is running.** Hierarchically probing the `SBToTL` bridge's own `wrEn`/`auto_out_a_valid` signals showed real bus activity only while the hart was held in reset; the `msip` write, issued *after* releasing reset, showed **zero** bus activity despite a "SUCCESS" DMI acknowledgment. Fixed by reordering: arm the interrupt *before* releasing `ndmreset`, not after.

**Bug 4 (root cause) — `sbcs` configuration write intermittently fails.** The `sbcs` write (enabling 32-bit access + autoincrement) was observed returning `dmi.resp = FAILURE` at the DMI transport layer. Without checking for this, `sbautoincrement` silently never took effect, so all 24 firmware words were overwriting the *same* address, and only the last word survived. Fixed generically: any DMI write whose response indicates failure is retried (same transaction, not advanced), and every `sbdata0` write is followed by a read-back-and-poll of `sbcs.sbbusy` before proceeding — exactly what a real hardware debugger does.

After fix 4, simulation showed the core correctly executing the real compiled firmware starting at `pc=0x80000000` (`lui sp,0x80004` → `jal main` → UART init with the correct baud divisor → `txen` enable → loading `'H'`), matching `objdump` byte-for-byte. This was confirmed on real hardware immediately after.

**Bug 5 — `.bss` never zeroed, discovered on real hardware.** After the 8-program extension, real-hardware testing of Conway's Game of Life produced a garbled, overly dense "generation 0" that died out almost immediately, instead of the seeded glider/blinker/toad pattern. The Game of Life *rules themselves* were correct (a dense random start overcrowding and collapsing within 1-2 generations is exactly what Conway's rules predict) — the bug was in the *starting state*. `grid`/`next_grid` in `gameoflife/main.c` (and, less visibly, `is_composite` in `primes/main.c`) are uninitialized globals, placed by the linker in `.bss`. C guarantees `.bss` reads as all-zero at program start, but that guarantee is normally delivered by a C runtime's startup code (`crt0`) running before `main()` — and this project's hand-written `boot.S` had no such step, jumping straight from `_start` to `call main`. The DTIM is real SRAM: a CPU reset does not clear its contents, so whichever bytes a *previously loaded program* left behind at that address range were still sitting there when Game of Life read them as its "empty" grid. This had been invisible until now because Hello World, Mandelbrot, and the calculator have no meaningful `.bss` state read before being written.

Fixed at the shared build-system level so every program benefits, not just Game of Life:

- **[`ninad/link.ld`](../link.ld)** — added `_bss_start`/`_bss_end` symbols bracketing the `.bss` output section, so `boot.S` has addresses to zero between.
- **[`ninad/boot.S`](../boot.S)** — added a byte-at-a-time zeroing loop between the stack-pointer setup and `call main`:
  ```asm
  la a0, _bss_start
  la a1, _bss_end
  bss_zero_loop:
      bgeu a0, a1, bss_zero_done
      sb zero, 0(a0)
      addi a0, a0, 1
      j bss_zero_loop
  bss_zero_done:
  ```
  A program with no `.bss` (`_bss_start == _bss_end`) takes the `bgeu` branch immediately, so this is a free no-op for programs like Hello World.

Both files are shared and copied into all 8 program directories, so all 8 firmware images were recompiled and `gen_dmi_rom_multi.py` re-run to regenerate `DmiAutoloader.v`'s ROM section (§8.2-8.3) — total DMI transactions grew from 1038 to **1094** (gameoflife's `.bss` alone is 1280 bytes, all needing an explicit zero-write instruction it didn't have before). The fix was checked by disassembling the rebuilt `gameoflife/firmware.elf`: `_bss_start` landed at `0x800002c4` (right after `grid`/`next_grid`) and `_bss_end` at `0x800007c4`, exactly 1280 bytes apart, matching the linker's reported `.bss` size — confirming the loop zeroes precisely the right range, no more and no less, before `main()` ever reads the grid.

---

## 8. Extension: Eight Selectable Programs

After the base Hello World / Mandelbrot demo was confirmed working on real hardware (both matched their host-simulated reference output exactly), the project was extended to **8 selectable programs**, chosen to demonstrate a spread of capability: string/UART output, fixed-point math, genuine bidirectional interactivity, iteration, sorting, cellular automata, and formatted numeric output.

| Index | SW3 SW2 SW1 | Program | Directory | Demonstrates |
|---|---|---|---|---|
| 0 | 0 0 0 | Hello World | `ninad/` | Basic UART TX |
| 1 | 0 0 1 | Mandelbrot ASCII art | `ninad/mandelbrot/` | Q8.8 fixed-point math (no hardware FPU on this core) |
| 2 | 0 1 0 | Calculator (interactive REPL) | `ninad/calculator/` | UART **RX** — the first program to use it |
| 3 | 0 1 1 | Fibonacci sequence | `ninad/fibonacci/` | Simple iteration |
| 4 | 1 0 0 | Prime sieve (up to 500) | `ninad/primes/` | Array-based algorithm (Sieve of Eratosthenes) |
| 5 | 1 0 1 | Bubble sort | `ninad/bubblesort/` | In-place sorting, before/after output |
| 6 | 1 1 0 | Conway's Game of Life | `ninad/gameoflife/` | 2D state, cellular automaton, 15 generations |
| 7 | 1 1 1 | Multiplication table | `ninad/timestable/` | Formatted tabular output |

### 8.1 Why 8, and why 3 select switches

One switch (as used for the original 2-program version) only encodes 2 choices. Three switches (`SW1`-`SW3`) encode 8 — enough for a meaningfully varied demo set without over-scoping. `SW0` remains the dedicated "arm" safety interlock, separate from program selection, so an accidental switch bump can't trigger a reload of the wrong program.

### 8.2 ROM architecture change: one flat array, not eight separate tables

The original two-program version used two entirely separate Verilog arrays selected by a ternary mux. That doesn't scale cleanly to 8 programs of very different sizes (24 to 266 data words each). It was replaced with:

- **One flat array** (`ALL_ADDR`/`ALL_DATA`) holding all 8 programs' DMI transactions concatenated back-to-back (1094 transactions total, after the `.bss`-zeroing fix in §7 Bug 5 added a few extra data words to programs with meaningful `.bss`).
- **Two small lookup tables**, `PROG_BASE[0:7]` and `PROG_LEN[0:7]`, recording where each program's transactions start and how many there are.
- At the moment `BTNU` is pressed, `DmiAutoloader` latches `prog_sel` (from `SW1`-`SW3`) once, looks up `base`/`len`, and walks exactly that slice of the flat array — the same proven request/response/retry/busy-poll state machine as before, just addressing `ALL_ADDR[base+idx]` instead of a hardcoded array.

This required widening the index registers from 10 bits to 11 bits partway through — 1038 transactions exceeds 10 bits' range (0-1023), a real bounds bug caught before it ever reached hardware, not after.

### 8.3 Never hand-typing ROM content again

The original Hello World table was hand-transcribed once (and cross-checked against `objdump` after the fact). For Mandelbrot, a hand-transcription mistake in the middle of the table was caught the same way — a `diff` against script output that didn't match. Rather than keep manually copying hex for 8 programs, **`ninad/basys3/gen_dmi_rom_multi.py`** was written to do the entire job mechanically:

```bash
cd ninad/basys3
python3 gen_dmi_rom_multi.py
```

It reads all 8 programs' compiled `firmware.mem` files, builds each program's full DMI transaction sequence (the same setup/data/CLINT-wake/release pattern used throughout this project), concatenates them, and **splices the result directly into `DmiAutoloader.v`** between two marker comments (`BEGIN GENERATED ROM SECTION` / `END GENERATED ROM SECTION`) via a Python string replace — never by retyping hex by hand. Adding a 9th program means adding one line to the `PROGRAMS` list and re-running the script.

### 8.4 Validation approach for each new program

Every program was checked at two independent levels before being trusted:

1. **Host-native compile.** Each program's UART register I/O macros were substituted with `putchar`/`getchar` via `sed`, compiled with the system `gcc`, and run directly on the development machine. This validates the *algorithm* (fixed-point math, sieve logic, sort correctness, Game-of-Life rules, calculator parsing/arithmetic) in seconds, independent of RISC-V toolchain or RTL simulation speed.
2. **RTL cross-check.** The furthest program in the flat ROM array (`timestable`, at index 7, `base=922`) was loaded in Verilator simulation and its first several retired instructions were compared against `objdump` of the real compiled binary — an exact match, confirming the new base+offset addressing logic is correct even at the extreme end of the table, not just for the first program.

The calculator's interactive behavior was specifically validated by piping realistic input at the host build: `12 + 7` → `19`, `6 * 7` → `42`, `100 / 3` → `33` (integer division), `5 - 20` → `-15`, `7 / 0` → correctly rejected, `banana` → correctly rejected as malformed, `-8 * -3` → `24`.

Full-length RTL simulation of every program's UART output was not run to completion for the newer programs (RTL cycle-accurate simulation of, e.g., a full 1872-pixel Mandelbrot render took over 10 minutes of wall-clock time for ~8 million simulated cycles and still hadn't finished — real hardware at 10MHz completes the same cycles in about a second). Given the algorithm-level and addressing-level validation above, and that Mandelbrot and Hello World's outputs were both confirmed to match exactly on real hardware, the remaining 6 programs were validated on real hardware directly rather than waiting on RTL simulation for marginal additional confidence.

---

## 9. Operating Instructions

**Program select switch settings** (SW3 is the leftmost/highest switch of the three, SW1 the rightmost/lowest):

| SW3 | SW2 | SW1 | Program |
|---|---|---|---|
| 0 | 0 | 0 | Hello World |
| 0 | 0 | 1 | Mandelbrot ASCII art |
| 0 | 1 | 0 | Calculator |
| 0 | 1 | 1 | Fibonacci sequence |
| 1 | 0 | 0 | Prime sieve |
| 1 | 0 | 1 | Bubble sort |
| 1 | 1 | 0 | Conway's Game of Life |
| 1 | 1 | 1 | Multiplication table |

Steps:

1. Program the bitstream (Hardware Manager → Program Device). The **DONE** LED lights when configuration succeeds.
2. Flip **SW0** up — **LD2** lights, confirming the loader is armed.
3. Set **SW1**-**SW3** to the binary code of the program you want, per the table above (`LD3`-`LD5` mirror them so you can confirm the selection before loading).
4. Press **BTNU** once — **LD0** lights while loading; **LD1** lights when the load (including all internal retries/polling) completes.
5. Open a serial terminal (PuTTY: Serial, correct COM port, 115200-8-N-1) **before** triggering the load, or press **BTNC** (reset) and repeat steps 3-4 with the terminal already open, since UART output is live, not buffered.
6. For the calculator (index 2), type an expression like `12 + 7` and press Enter in the terminal.

---

## 10. File Index

| File | Purpose |
|---|---|
| `generators/chipyard/src/main/scala/config/RocketConfigs.scala` | `TinyRocketDMIConfig` + `WithSBADebugModule` (edited) |
| `ninad/main.c`, `ninad/link.ld`, `ninad/boot.S` | Program 0: Hello World |
| `ninad/mandelbrot/main.c` (+ shared `link.ld`/`boot.S`) | Program 1: Mandelbrot |
| `ninad/calculator/main.c` | Program 2: Calculator (UART RX) |
| `ninad/fibonacci/main.c` | Program 3: Fibonacci |
| `ninad/primes/main.c` | Program 4: Prime sieve |
| `ninad/bubblesort/main.c` | Program 5: Bubble sort |
| `ninad/gameoflife/main.c` | Program 6: Game of Life |
| `ninad/timestable/main.c` | Program 7: Multiplication table |
| `ninad/basys3/BasysTop.v` | Top-level Vivado wrapper (3-bit program select) |
| `ninad/basys3/DmiAutoloader.v` | On-chip DMI/SBA loader sequencer (8-program flat ROM) |
| `ninad/basys3/gen_dmi_rom.py` | Single-program DMI ROM generator (original, still used for reference) |
| `ninad/basys3/gen_dmi_rom_multi.py` | Multi-program DMI ROM generator -- assembles and splices all 8 programs into `DmiAutoloader.v` |
| `ninad/basys3/basys3.xdc` | Pin constraints (now includes SW1-SW3, LED3-LED5) |
| `ninad/basys3/vivado_sources/` | Staged, complete synthesizable design (265 files, from Chipyard) |
| `ninad/basys3/vivado_sources/EICG_wrapper.v` | Hand-patched clock-gate (timing fix) |
| `ninad/basys3/tb_dmi.v` | Verilator debug testbench (simulation only, not synthesized) |
