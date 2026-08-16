# Architecture Decisions Record (ADR)

## ADR-001: CPU Choice - BOOM over Rocket
**Date**: Aug 16, 2026  
**Status**: ✅ APPROVED

**Decision**: Use Berkeley Out-of-Order Machine (BOOM) as the base processor for the DOOM challenge.

**Rationale**:
- **Performance**: Doom3 has demanding rendering math (floating-point heavy); BOOM's 4-wide superscalar execution outperforms Rocket's 1-wide pipeline significantly
- **Technical interest**: BOOM showcases real processor design challenges (reorder buffer, pipeline hazards, dependency tracking); more interesting than Rocket's simpler pipeline
- **Fallback available**: If BOOM exhausts FPGA resources, fallback to Rocket core (proven on F1, minimal modifications needed)

**Alternatives considered**:
- **Rocket (5-stage)**: Simpler, more FPGA-friendly, but single-issue limits Doom3 performance
- **BOOM Large (6-wide)**: Better performance, but higher resource usage (risky for FPGA)
- **Custom minimal core**: Educational value, but 21-day timeline too aggressive

**Consequences**:
- Estimated FPGA resource usage: ~180K LUTs, ~1.2K BRAMs (tight but feasible)
- Synthesis time: 4–6 hours per iteration (expected)
- Debugging complexity: Higher (more complex pipeline, reorder logic)

**Owner**: ninadjangle  
**Related**: [CLAUDE.md](CLAUDE.md) — CPU Design section

---

## ADR-002: BOOM Configuration - MediumConfig (4-wide, 8-stage)
**Date**: Aug 16, 2026  
**Status**: ✅ APPROVED

**Decision**: Start with MediumBOOMConfig (4-wide issue, 8-stage pipeline) as the baseline CPU configuration.

**Parameters**:
- Fetch width: 2 instr/cycle
- Decode width: 2 instr/cycle
- Issue width: 4 instr/cycle (4 integer, can tune)
- ROB entries: 64
- Physical registers: 100 integer, 64 floating-point
- L1-I: 32KB, 1-way (direct-mapped)
- L1-D: 32KB, 2-way
- L2: 256KB, 4-way
- System bus: 64-bit @ system clock

**Rationale**:
- **Balanced trade-off**: 4-wide issue provides sufficient ILP for Doom3 without excessive resource overhead
- **Conservative caches**: 256KB L2 is reasonable for embedded workloads; can grow to 512KB if needed
- **FPGA feasibility**: Estimated ~80% LUT utilization on AWS F1.2xlarge (headroom for optimization)
- **Testable**: Medium complexity — not trivial, not trivially small

**Alternatives considered**:
- **SmallBOOMConfig (2-wide, 6-stage)**: Lighter footprint, ~40% fewer LUTs, but 20–30% performance penalty
- **LargeBOOMConfig (6-wide, 10-stage)**: Better performance, but pushes FPGA limits; synthesis may fail

**Escalation plan**:
- If synthesis fails → switch to SmallBOOMConfig (proven path)
- If performance insufficient on Doom3 → optimize L2, increase frequency

**Owner**: ninadjangle  
**Related**: ADR-001, [CLAUDE.md](CLAUDE.md) — CPU Design section  
**Status**: Pending RTL synthesis validation (Day 2)

---

## ADR-003: ISA Extensions - RV64IMAFDv
**Date**: Aug 16, 2026  
**Status**: ✅ APPROVED

**Decision**: Support RV64IMAFDv RISC-V ISA extensions (64-bit base with M, A, F, D, vector).

**Breakdown**:
- **RV64I**: 64-bit base integer (required)
- **M**: Multiply/Divide (useful for graphics math)
- **A**: Atomic operations (enables Linux SMP and synchronization)
- **F/D**: Floating-point single/double precision (critical for Doom3 rendering)
- **v**: Vector extension (deferred — enable if time permits, stretch goal)

**Rationale**:
- **Doom3 requirements**: Heavy use of floating-point (trig, matrix ops, lighting calcs)
- **Linux support**: F/D and A are expected by modern RV64 kernels
- **Hardware cost**: F/D adds ~10–15% to LUT count (acceptable); vector can be deferred

**Implications**:
- Bootloader must set floating-point context in trap handler
- Compiler flags: `-march=rv64imafd -mabi=lp64d`
- Cross-compiler: Must support RV64IMAFDv (gnu-toolchain does)

**Owner**: ninadjangle  
**Related**: [CLAUDE.md](CLAUDE.md) — ISA section  
**Status**: ✅ Approved; deferred vector extension for Phase 3 optimization

---

## ADR-004: Memory Configuration - 2GB Physical, 32KB/32KB/256KB Caches
**Date**: Aug 16, 2026  
**Status**: ✅ APPROVED

**Decision**: Target 2GB physical memory with conservative cache sizes (32KB L1-I/D, 256KB L2).

**Memory Layout**:
- **0x00000000–0x00001000**: Boot ROM (exceptions, interrupt vectors)
- **0x00001000–0x01000000**: User space (heap + stack grow down)
- **0x80000000–0x80100000**: Kernel boot area (device tree, boot ROM)
- **0x80000000–0xFFFFFFFF**: Physical DRAM up to 2GB
- **0xFFFF000000000000**: Kernel virtual space (Sv48 paging, 64-bit)

**Cache sizes**:
- **L1-I**: 32KB, 64 sets × 512B lines × 1-way (direct-mapped)
- **L1-D**: 32KB, 64 sets × 512B lines × 2-way
- **L2**: 256KB, 512 sets × 512B lines × 4-way

**Rationale**:
- **Conservative approach**: Doom3 has ~100MB+ code/data; 256KB L2 covers hot working set
- **FPGA feasibility**: SRAMs are abundant; no DRAM controller complexity initially (use single-port DRAM)
- **Scalability**: Can increase to 512KB/1MB L2 post-synthesis if headroom exists
- **Simulation**: Start with 512MB in Verilator (faster), scale to 2GB on FPGA

**Consequences**:
- No prefetching initially (can add later)
- L1-I direct-mapped (simpler, no conflict issues expected)
- 256KB L2 may be tight for Doom3 optimization phase; re-evaluate Day 15

**Owner**: ninadjangle  
**Related**: [CLAUDE.md](CLAUDE.md) — Memory Layout section  
**Status**: ✅ Approved; post-Day-3 reassessment recommended

---

## ADR-005: Build System - Chisel 6.7.0 + Mill + Verilator
**Date**: Aug 16, 2026  
**Status**: ✅ APPROVED

**Decision**: Use Chisel 6.7.0, Mill build system, and Verilator for RTL simulation.

**Toolchain versions**:
- **Chisel**: 6.7.0 (latest stable in Chipyard)
- **Mill**: SBT-based Scala build tool (bundled with Chipyard)
- **RISC-V toolchain**: gnu-toolchain (riscv64-unknown-elf-gcc)
- **RTL simulator**: Verilator (fast C++ simulation; ~100–200x speedup over Verilog sim)
- **FPGA synthesis**: Vivado 2023.x (via FireSim flow)
- **Bootloader/firmware**: RISC-V assembly + minimal C

**Rationale**:
- **Chisel 6.7.0**: Proven in Chipyard, stable, good documentation
- **Mill**: Faster incremental builds than SBT; good for iteration
- **Verilator**: Fastest for behavioral simulation; standard practice
- **RISC-V toolchain**: GNU tools well-supported, open-source, cross-platform

**Alternatives rejected**:
- Chisel 7.x: Not yet stable in this Chipyard version
- Vivado HLS/P&R automation: Risk/complexity not worth 21-day timeline
- Spike ISA simulator: Good for debugging, but won't validate RTL (used in parallel)

**Build commands**:
```bash
# Compile Chisel to Verilog
mill generators.doom-boom.compile
make CONFIG=DoomBoomConfig verilog

# Simulate with Verilator
make -C sims/verilator CONFIG=DoomBoomConfig run BINARY=test.riscv

# Synthesize (Vivado, Phase 2)
firesim buildbitstream
```

**Owner**: ninadjangle  
**Related**: [CLAUDE.md](CLAUDE.md) — Toolchain section; [tasks-day1-to-7.md](tasks-day1-to-7.md)  
**Status**: ✅ Approved; environment pre-configured

---

## ADR-006: AWS FPGA Deployment Strategy - FireSim + F1 Instances
**Date**: Aug 16, 2026  
**Status**: ✅ APPROVED (pending AWS access verification Day 1)

**Decision**: Use AWS EC2 F1 instances with FireSim for FPGA deployment.

**Deployment flow**:
1. **Local RTL simulation** (Verilator): Days 1–3
2. **FireSim bitstream generation** (Vivado on AWS): Days 4–5
3. **AWS F1 instance deployment**: Days 6–7
4. **Linux kernel bring-up**: Days 8–14
5. **Doom3 execution**: Days 15–21

**AWS Resources**:
- **Instance type**: f1.2xlarge (1x Xilinx VU9P FPGA, 16GB RAM, 8 vCPU)
- **Control instance**: t3.large (if needed for CI/orchestration)
- **Storage**: ~200GB EBS for bitstream + toolchain
- **Network**: VPC with EFT (Ethernet-over-FPGA) for console access

**Rationale**:
- **FireSim integration**: Chipyard + FireSim pairing is seamless; automated bitstream → FPGA flow
- **F1 availability**: Production-grade FPGA, well-documented in FireSim ecosystem
- **Cost**: ~$2–5/hour dev machine; ~$100 total project budget (acceptable)
- **Scalability**: Can parallelize bitstream generation across multiple instances

**Alternatives rejected**:
- **Local FPGA board** (Basys3, etc.): Limited I/O, slower iteration; AWS better for prototyping
- **Google Cloud TPU/FPGA**: Overkill, less mature RISC-V support
- **Open-source toolchain** (yosys, nextpnr): Xilinx VU9P unsupported (closed-source bitstream)

**Risks & Mitigations**:
| Risk | Likelihood | Mitigation |
|------|-----------|-----------|
| AWS quota exceeded | Low | Request increase Day 1; fallback: local machine synthesis |
| Vivado license issues | Low | AWS provides license via developer AMI |
| Synthesis timeout | Medium | Parallelize, debug timing early |
| FPGA bitstream too large | Medium | Downgrade L2 to 128KB, reduce frequency |

**Owner**: ninadjangle  
**Status**: ✅ Pending AWS access check (Day 1, Step 7b)  
**Related**: [CLAUDE.md](CLAUDE.md) — AWS FPGA Deployment section; [tasks-day1-to-7.md](tasks-day1-to-7.md) Days 4–7

---

## ADR-007: Linux Kernel Version - RV64 Mainline 5.x+
**Date**: Aug 16, 2026  
**Status**: ✅ APPROVED (deferred to Phase 2, Day 8)

**Decision**: Use Linux mainline kernel (5.x or 6.x) with RV64 support for Phase 2 bring-up.

**Kernel configuration**:
- **Base**: Linux 5.19+ (proven RV64 support)
- **Arch**: RISC-V 64-bit (RV64I, with M/A/FD support)
- **Drivers**: Minimal (UART, timer, paging only)
- **Device tree**: Custom for BOOM CPU (register addresses, interrupt config)

**Rationale**:
- **RV64 support**: Mainline kernel has excellent RISC-V support as of 5.x+
- **Minimal configuration**: Keep kernel <8MB (fits in ROM + RAM budget)
- **Proven**: Widely used in RISC-V community; debugging resources abundant

**Build approach** (Phase 2):
```bash
# Configure kernel for RV64
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- \
  menuconfig > linux-config-rv64-doom

# Build kernel + modules
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- -j8

# Create device tree for DOOM CPU
# (custom .dts file describing CPU, memory, UART)
```

**Owner**: ninadjangle  
**Status**: 🟡 Deferred to Day 8 (Phase 2)  
**Related**: [CLAUDE.md](CLAUDE.md) — Software Stack section; [tasks-day1-to-7.md](tasks-day1-to-7.md) Days 8–14

---

## ADR-008: DOOM Port Selection - Doom3 or Lightweight Variant
**Date**: Aug 16, 2026  
**Status**: ⏳ PENDING (deferred to Day 15, Phase 3)

**Decision**: Target Doom3 or modern DOOM port for interactive execution on RISC-V FPGA.

**Candidates**:
1. **Original Doom (1993)**: ~3MB binary, 2.5MB data (very tight, but possible)
2. **Doom3**: ~200MB data, heavy floating-point, complex asset loading
3. **Doom3 BFG Edition**: Enhanced version with better rendering, higher resource footprint
4. **PrBoom+**: Open-source Doom port, lighter, well-maintained
5. **Custom minimal renderer**: Implement triangle rasterizer from scratch (100–200 LOC C)

**Current plan**:
- Attempt Doom3 (ambitious but achievable)
- Fallback to PrBoom+ or custom renderer if porting blocked

**Rationale** (Phase 3 decision):
- **Doom3** showcases full processor capability (floating-point, caching, memory hierarchy)
- **Engineering interest**: Custom graphics backend for framebuffer provides learning
- **Timeline buffer**: Lightweight fallback available if needed

**Owner**: ninadjangle  
**Status**: ⏳ To be decided Day 15 based on Phase 2 progress  
**Related**: [CLAUDE.md](CLAUDE.md) — Software Stack, Phase 3  
**Next decision**: Day 14 (end of Phase 2)

---

## ADR-009: Optimization Strategy - Performance Tuning Post-Day-14
**Date**: Aug 16, 2026  
**Status**: ⏳ PENDING (deferred to Day 15, Phase 3)

**Decision**: Focus Phase 1–2 on **correctness**, defer **optimization** to Phase 3 (Days 15–21).

**Phase 1–2 (Days 1–14)**: Correctness First
- CPU design validated via simulation
- Bootloader proves execution
- Linux kernel boots to shell
- **No optimization**: Extra frequency, prefetch, cache tuning deferred

**Phase 3 (Days 15–21)**: Optimization
- Benchmark Doom3 framerate (target: 30+ FPS)
- Profile CPU (cache misses, branch mispredicts)
- Tune cache sizes, prefetch policy, pipeline depth
- Increase frequency if timing allows

**Rationale**:
- **Risk management**: Over-optimization early can break things; better to iterate on stable base
- **Time box**: 21 days is tight; focus on critical path first
- **Flexibility**: Easy to optimize if ahead of schedule; hard to debug crashes on tight timeline

**Owner**: ninadjangle  
**Status**: 🟡 Under review (may evolve as project progresses)  
**Related**: [CLAUDE.md](CLAUDE.md) — Success Criteria section

---

## Decision Log Template (for future entries)

```
## ADR-NNN: [Title]
**Date**: [YYYY-MM-DD]
**Status**: ✅ APPROVED | 🟡 PENDING | ❌ REJECTED

**Decision**: [What was decided?]

**Rationale**: [Why?]

**Alternatives**: [What was rejected and why?]

**Consequences**: [What changes as a result?]

**Owner**: ninadjangle

**Related**: [Links to other ADRs, docs, code]
```

---

**Last Updated**: Aug 16, 2026 | **Owner**: ninadjangle | **Total ADRs**: 9 (6 approved, 2 pending, 1 rejected)
