# INT8 tile-engine accelerator + on-chip LLM inference / LoRA fine-tuning

Custom RoCC accelerator (`generators/chipyard/src/main/scala/accel/Int8TileEngine.scala`),
8x8 INT8 matmul tile with free operand transpose, on Rocket at 50 MHz (Arty A7-100T).
Config: `RocketArty100TInt8Config` (hardware), `chipyard.accel.RocketInt8AccelSimConfig` (Verilator).

## Verified results (real board unless stated)
| what | result |
|---|---|
| engine self-test (`accel_test.c`) | ALL PASS in RTL sim and on hardware; 104 cyc vs ~10.8k cyc in C per 8x8x8 tile |
| tiled GEMM (`int8_gemm.h`, `gemm_test.c`) | 16x24x32, all transpose modes + int8 out: matches C ref (RTL sim); ~120x faster than C |
| engine stress on hardware | 7,417 GEMM iterations and 7,907 full LLM forward passes, 0 mismatches (clean start) |
| TinyStories-260K INT8 inference | engine logits bit-identical to plain-C logits; float ref agreement 40/40 tokens |
| forward pass speed | engine 886k cyc vs software 3.62M cyc (4.1x) |
| on-chip LoRA fine-tune (`llm/llm_final.c`, 260K) | loss 1.8889 -> 0.0183 in 30 epochs (host: 1.8890 -> 0.0184); 3.86 s/epoch; base "girl named Lily" -> tuned "robot named Arty ... friend named Ninad"; 8 train steps 4.11x faster than software |

| **15M-parameter model** (TinyStories-15M, dim 288, 6 layers, vocab 32000, 15.7 MB INT8) | engine forward bit-identical to software; training steps **7.8x faster** than software (319M vs 2487M cyc / 3 steps); on-chip LoRA (Adam, rank 8, last-layer w2): loss 2.89 -> 0.0001 in 20 epochs (84 s/epoch), tuned model reproduces the new story instead of the Lily story |
Console logs: `results/`.

## Reproduce
1. Bitstream: `make -C fpga SUB_PROJECT=arty100t CONFIG=RocketArty100TInt8Config verilog` then
   `make ... <build_dir>/<name>.vsrcs.f`; copy to `C:\arty100t-build`, rewrite `/home/...` paths in vsrcs.f,
   append `gen-collateral/Arty100TDmiAutoloader.sv`, run Vivado with the 4 IP tcls (mig, pll, shell, **ila_gen**).
   Timing: WNS +0.274 ns; hold WHS -0.020 ns, all in the Debug Module (not CPU/engine).
2. Model: `cd llm; python3 quantize.py stories260K.bin tok512.bin stories260K_i8.bin`
   (checkpoints from huggingface karpathy/tinyllamas). `encode.py` tokenizes the fine-tune text.
3. Host check: `gcc -O2 llm_host.c -lm; ./llm_host stories260K_i8.bin gen 150 0 1`; `ref_float.py`, `quant_sim.py`.
4. Board: program bitstream, then `uart_tsi +tty=<Digilent if01> +baudrate=921600 +no_hart0_msip +init_read=0x80000000 llm_final.elf`;
   read the FT232R console at 115200.

## Root cause of the "layout-dependent stall" (SOLVED)
`fpga/src/main/scala/arty100t/DmiAutoloader.scala` writes two boot "checkpoint" sentinels,
0xAAAAAAAA at **0x80001004** and 0xF00DF00D at **0x80001008**, into what it calls "unused DRAM scratch".
Any image larger than 4 KB owns those addresses, so two words of code or data were silently replaced
(found by checksumming a 15.7 MB blob on the board against the host: exactly two words differed). Whether a
program survived depended on whether those words landed in dead code -> the "deterministic per-binary,
layout-dependent" stalls (and NaNs with the 15M weights).
**Not the DOOM crash, though** (checked): in the earlier DOOM images those two words fall inside the automap code
(AM_drawGrid/AM_drawMline), which the attract loop never runs, and a DOOM build with the *old* layout but the start
gate runs fine (see DOOM A/B below).
Workaround (no bitstream change): `accel_link.ld` keeps only the start gate below 0x1000 and starts all other
sections at 0x80002000. Proper fix: move the two checkpoint addresses in DmiAutoloader.scala (needs a rebuild).

## Second load-time hazard (SOLVED): no barrier between "load finished" and "core starts"
The core starts running while `uart_tsi` is still streaming the ELF, so big images run on half-loaded memory.
`accel_start_gated.S` spins on a DRAM flag (0x86100000 == 0x600DF00D) that uart_tsi writes after the load:
`uart_tsi ... +init_write=0x86100000:0x600DF00D image.elf`. Build with `-Wl,-T,../accel_link.ld`.

## Other notes
* Always reprogram the FPGA before loading a new ELF (each uart_tsi connection also resets the SoC, so reading
  DRAM through it after a crash is not a valid probe).
* LoRA is on the last layer's FFN down-projection only (gradient flows through the tied classifier + final RMSNorm);
  the classifier-transpose product uses the engine's transposed-A mode. 15M model uses Adam + grad-clip (SGD diverges).

## Build/run recipe for the corrected flow (used for all final results)
```
riscv64-unknown-elf-gcc -march=rv64imafdc -mabi=lp64d -mcmodel=medany -O2 -ffreestanding -fno-math-errno \
  -nostdlib -nostartfiles -Wl,-T,../accel_link.ld -Wl,--gc-sections ../accel_start_gated.S llm15_train_v2.c model_blob15.S -o out.elf -lgcc
# reprogram FPGA, then:
uart_tsi +tty=<Digilent if01> +baudrate=921600 +no_hart0_msip +init_read=0x80000000 +init_write=0x86100000:0x600DF00D out.elf
```
Training-text note: a rare end-of-text token (EOS) in the fine-tune text prevents convergence for the 15M model (loss floors ~0.74,
junk tokens appear); the final text has none. Rare names ("Ninad") are also hard for a rank-8 last-layer adapter at 15M.

## DOOM A/B on the same bitstream (RocketArty100TConfig), all from a clean reprogram
| build | outcome (logs in results/doom_*) |
|---|---|
| original ungated image (`doom-arty100t-console.elf`) | prints the startup line, clears the screen, then `[doom] exited with code -1` |
| start gate + OLD layout (`...-gated-oldlayout.elf`) | initializes fully and renders frames continuously (156+ frames, no traps) |
| start gate + sentinel hole (`...-gated.elf`) | same, 433+ frames / 5+ minutes, no traps |
So the load barrier (core running while `uart_tsi` is still streaming the ELF) is what broke DOOM's startup; the
sentinel words only mattered for images where they land in executed code (the LLM programs).

## Interactive chat over serial + on-chip learning (2026-09-26)
`llm/llm_chat.h` (platform-independent) + `llm_chat_arty.c` (UART wrapper) + `llm_chat_host.c` (PC harness).
On the board (FT232R console, 115200): type a prompt, the 15M model continues it; `/train [epochs] <text>` LoRA-fine-tunes
on the chip; `/base` `/tuned` `/reset` `/temp` `/len`. Tokenizing is done ON the chip (BPE with a hash table; merge scores
are a new last section of the blob, `tok_scores`). Verified on the real board:
* prompt "Once upon a time, there was a little dog named Max." -> coherent continuation, 14 prompt tokens (matches the Python encoder), 1.5 tok/s
* `/train 12 The robot named Arty lived on a tiny green board.` -> loss 6.22 -> 0.0004 in 12 epochs (about 26 s/epoch), then the
  prompt "The robot named" gives "Arty lived on a tiny green board." (adapter off: "one day, it was very hot outside.")
Renderings of the real captured logs: `results/screens/`. Type into the console with `tools/chip_say.py "text"`.
Tools: `tools/fresh_load.sh` (reprogram + load + capture), `tools/build_bitstream.sh` (Windows Vivado pipeline),
`tools/test_engine_on_board.sh` (self-test, GEMM test, stress tests on the board).

## Systolic-array engine (`SystolicTileEngine.scala`)
Same RoCC ISA (software unchanged); only the multiply stage is replaced by an output-stationary 8x8 grid of PEs with
neighbour-to-neighbour data flow and skewed injection (fan-out 1 instead of 8). Configs: `RocketSystolicAccelSimConfig`,
`RocketArty100TSystolicConfig`. RTL simulation: `accel_test` ALL PASS (117 vs 104 cycles per tile: the skew adds ~12 cycles of
latency), `gemm_test` all four transpose modes ok. Hardware status: see the bottom of this file.
