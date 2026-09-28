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

### Systolic engine: hardware results (2026-09-26)
Bitstream `RocketArty100TSystolicConfig` built with Vivado: **all timing constraints met** (setup WNS +0.053 ns, hold WHS +0.053 ns,
0 failing endpoints; the older broadcast-engine bitstream had a -0.020 ns hold violation in the debug module). On the board:
* engine self-test ALL PASS (117 cycles per load+multiply+store tile vs 104 for the broadcast engine)
* tiled GEMM test ALL PASS (all four transpose modes + int8 output)
* stress: 4,121 GEMM iterations and 4,206 full 260K-LLM forward passes in 90 s, 0 mismatches (bit-identical to software)
* chat: 15M model at 1.4-1.5 tok/s (broadcast engine: 1.5-1.7 tok/s); `/train` LoRA works (loss 6.22 -> 0.0100 in 8 epochs, 26.9 s/epoch);
  the tuned model completes "The robot named" with "Arty lived on a tiny green board."
Honest takeaway: the systolic array is functionally equivalent and cleaner for timing, but ~10% SLOWER here, because each tile pays
an extra ~12 cycles of skew latency while memory traffic (not the multiplier layout) is the bottleneck. Its advantage is that it scales
(fan-out 1, neighbour-only wires) once loads are overlapped with compute. Raw logs: `results/board_tests_systolic/`, renders: `results/screens/`.

## Session 2026-09-27: benchmark, SmolLM, DSP-mapped systolic array, batching (all measured on the board)

### CPU vs engine, 15M TinyStories model, generating text (bench15.c, `results/bench/`)
| build | cycles/token | tokens/s | vs CPU | train step vs CPU | LUTs | DSPs | setup / hold slack (ns) |
|---|---|---|---|---|---|---|---|
| plain CPU (no engine) | 200.4M | 0.24 | 1x | 1x | 46,596 | 25 | n/a |
| broadcast engine | 28.65M | 1.74 | 7.0x | 7.6x | 55,037 | 27 | +0.274 / -0.020 (RISC-V debug module) |
| systolic (LUT multipliers) | 31.74M | 1.57 | 6.3x | 7.3x | 55,050 | 27 | +0.053 / +0.053 |
| systolic (DSP slices) | 31.74M | 1.57 | 6.3x | 7.3x | 49,103 | 91 | +0.163 / 0.000 |
Matrix-vector multiplies are 95-96% of engine-time per token; the same first tokens come out in CPU and engine mode.
The DSP-mapped array (`SystolicDspTileEngine`: each PE is a `use_dsp` black box) does the same work in ~6,000 fewer LUTs
(engine logic ~8.4k -> ~2.5k LUTs) and uses exactly 64 more DSP slices. It passes the self-test, GEMM test, 4,200 GEMM and
4,304 LLM-forward stress iterations with 0 mismatches (`results/board_tests_dsp/`).

### SmolLM2-135M-Instruct on the board (`smol/`)
* 30 layers, 135M parameters, INT8 image 135.5 MB in the 256 MB DDR3 (`0x80000000..0x88xxxxxx`); stack/gate flag/trap slots moved to the top
  of DRAM (`smol_start_gated.S`). Load takes ~28 min at 921,600 baud. The PC tokenizes (byte-level BPE) and decodes; ids go over the serial link
  (`smol_chat.py`).
* On-chip self-check `C`: 98,304 logits (2 forward passes) engine vs plain CPU: **0 mismatches**; DSP-systolic 271.7M vs 1775.8M cycles/forward
  (6.53x), broadcast 242.9M vs 1638.5M (6.74x; earlier firmware build, CPU-only cycles differ ~8% between builds).
* Batched prefill (`llm_batch.h`): 8 known tokens go through each layer together so the tile uses all 8 columns. Bit-identical to token-by-token
  (SmolLM on the board: 16 tokens, 49,152 logits, 0 mismatches, 4.39G -> 0.85G cycles = **5.1x**; 15M model, 32 tokens: 1.53 -> 7.64 tok/s = **5.0x**
  on the DSP bitstream, 4.8x on broadcast). Generation still uses 1 of 8 columns, so it stays at ~5.4 s/token.
* Quality (PC check, `smol/quant_test_smol.py`): the engine's exact arithmetic (per-row INT8 weights, per-vector INT8 activations) picks the same next
  token as float ~88-89% of the time (teacher-forced), weights-only ~94-95%. "The capital of France is Paris." is identical in float and INT8.
  Chip vs PC-C can differ at near-ties (fused multiply-add rounding): the PC build with FMA also continues past "Paris." like the chip does.
* Repetition penalty (default 1.15 in `smol_chat.py`) fixes greedy loops such as the haiku "A chip that can think," x4.

### Video/assets tooling
`tools/serial_log.py` (timestamped capture), `tools/replay_to_video.py` (re-draws a real capture at its captured timing; badges any time compression),
`tools/render_systolic_anim.py` (cycle-accurate animation, result checked against A x B), `tools/render_terminal.py` (terminal-style render of real logs).


## SmolLM2-135M LoRA fine-tuning on the chip (added, host-verified)
`/train <question> :: <answer>` (interactive) or `--train ... --answer ... --then ...` (one-shot) in `smol_chat.py`.
Same generic LoRA machinery used for TinyStories, now wired into `smol_arty.c` (`T`/`O`/`F` serial commands) and
pointed at SmolLM's dimensions (rank-8 adapter, ~17K trainable parameters on the last layer's FFN down-projection,
vs ~6.1K for the 15M model). Needed one real fix: the batched prefill path (`forward_batch_ex`, our 5x prompt
speedup) did not apply the LoRA adapter at all -- only single-token `forward()` did. Patched so the adapter is
applied to the final position of the last prefill chunk too; verified bit-identical to sequential `forward()`
under LoRA (0 of 49,152 logits differ, `smol/batch_lora_test.c`).

Host-verified (`smol/smol_host_v4.c`, emulates the exact chip protocol), with proper context resets between tests:
* Before training, "What chip are you running on?" -> hallucinates an Intel Core i7.
* Trained 20 epochs on that question -> answer "I am running on a custom RISC-V chip with a homemade INT8 matrix
  accelerator." Loss 3.14 -> 0.0008.
* Same exact question after training -> reproduces the trained answer exactly.
* Paraphrase "What hardware powers you?" (never seen in training) -> also gives the trained answer: generalizes,
  not pure memorization.
* Unrelated question ("What is the capital of France?"), adapter ON but with the conversation reset first ->
  "The capital of France is Paris." -- unaffected. (An earlier test without resetting context between turns showed
  the trained answer bleeding into unrelated questions; that was accumulated CHAT CONTEXT from prior turns, not the
  adapter -- confirmed by isolating each test with a reset.)
* `/base` -> `/tuned` correctly switch the adapter off and back on.
Board load (firmware v4, `smol_chat_v4.elf`) and on-chip verification: see `results/smol/` for the run this produced.
