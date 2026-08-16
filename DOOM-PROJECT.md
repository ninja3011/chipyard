# DOOM on RISC-V - 21 Day Challenge

> **Goal**: Build a custom BOOM-based RISC-V CPU on AWS FPGA, running Doom3 or a modern DOOM port. From RTL to interactive game in 21 days.

## Quick Start (Phase 1 Preview)

```bash
cd /home/ninadjangle/chipyard
git checkout doom-challenge-phase1

# Activate build environment
source scripts/conda/conda-env-activate.sh env

# Build BOOM RTL (Day 2+)
mill generators.doom-boom.compile
make CONFIG=DoomBoomConfig verilog

# Simulate (Day 3+)
make -C sims/verilator CONFIG=DoomBoomConfig run BINARY=tests/doom-boom/factorial.riscv
```

## 21-Day Timeline & Phase Goals

### Phase 1: CPU Foundation (Days 1–3)
**Goal**: Design BOOM CPU, verify RTL simulation, prove execution.

- **Day 1** (Aug 16): Architecture decisions + project setup
  - ✅ Feature branch created
  - ✅ CLAUDE.md (architecture spec)
  - ✅ Task breakdown + decisions log
  - ✅ Build environment verified
  
- **Day 2** (Aug 17): RTL simulation environment
  - [ ] BOOM MediumConfig finalized in Chisel
  - [ ] Verilator simulator built
  - [ ] Factorial test program written + compiled
  
- **Day 3** (Aug 18): Bootloader + bare-metal proof
  - [ ] Minimal bootloader (exception handler, UART init, stack)
  - [ ] Hello-world bare-metal app
  - [ ] Bootloader → app execution verified in Verilator

**Phase 1 Deliverable**: CPU running simple programs in RTL simulation ✅

---

### Phase 2: FPGA & Kernel (Days 4–14)
**Goal**: Deploy to AWS FPGA, bring up Linux kernel.

**Days 4–7**: FPGA Synthesis & AWS Deployment
- [ ] FireSim configuration for AWS F1
- [ ] Bitstream generation (Vivado, 4–6 hours)
- [ ] AWS EC2 F1 instance provisioning
- [ ] Bootloader → kernel execution on real FPGA

**Days 8–14**: Linux Kernel Bring-Up
- [ ] RV64 Linux kernel configuration
- [ ] Device tree setup (UART, timer, memory)
- [ ] Minimal root filesystem (buildroot)
- [ ] Boot to shell prompt on FPGA
- [ ] Verify /proc/cpuinfo, dmesg, basic utilities

**Phase 2 Deliverable**: Linux shell running on FPGA ✅

---

### Phase 3: Doom3 Port & Optimization (Days 15–21)
**Goal**: Run Doom3/DOOM on the custom CPU.

**Days 15–18**: DOOM Porting
- [ ] Identify DOOM source (Doom3, id1, or lightweight port)
- [ ] Cross-compile for RV64
- [ ] Custom framebuffer graphics backend
- [ ] Load + execute on FPGA

**Days 19–21**: Optimization & Final Validation
- [ ] Performance profiling (CPU, cache, memory)
- [ ] Cache optimization (if needed)
- [ ] Gameplay validation (30+ FPS target)
- [ ] Final video/demo capture

**Phase 3 Deliverable**: DOOM running interactively on FPGA ✅

---

## Architecture Summary

| Component | Choice | Rationale |
|-----------|--------|-----------|
| **CPU** | BOOM MediumConfig | 4-wide superscalar, 8-stage pipeline; balance performance vs FPGA resources |
| **ISA** | RV64IMAFDv | 64-bit, includes int, mult, atomic, floating-point for Doom3 math |
| **Cache** | 32KB L1-I/D, 256KB L2 | Conservative; can optimize if needed |
| **Memory** | 2GB physical | Supports Doom3 + kernel + utilities |
| **Build** | Chisel 6.7.0 + Mill | Latest stable in Chipyard |
| **Simulate** | Verilator | Fast C++ RTL simulation for iteration |
| **FPGA** | AWS F1 (FireSim) | Production-grade FPGA, automated bitstream generation |
| **OS** | Linux RV64 | Full software stack support |
| **DOOM** | Doom3 or modern port | Engaging interactive workload |

For detailed architecture decisions, see [CLAUDE.md](CLAUDE.md).

---

## Project Status

### Current (Day 1)
- ✅ Feature branch: `doom-challenge-phase1`
- ✅ Architecture finalized (BOOM MediumConfig, RV64IMAFDv)
- ✅ Project structure established (CLAUDE.md, tasks, decisions log)
- ✅ Build environment verified
- ⏳ **Next**: RTL simulation environment (Day 2)

### Blockers / Risks
- **Synthesis time** (4–6 hrs): Parallelize if possible
- **AWS quota**: Verify F1 access on Day 1
- **DOOM porting**: Identify source + licensing early

---

## File Guide

- **[CLAUDE.md](CLAUDE.md)** — Architecture decisions, constraints, success criteria
- **[decisions.md](decisions.md)** — Architecture decision record (ADR) log
- **[tasks-day1-to-7.md](tasks-day1-to-7.md)** — Detailed daily task breakdown
- **generators/doom-boom/** — Custom BOOM CPU configuration (Chisel)
- **bootloader/** — Minimal boot ROM (assembly)
- **software/** — Bare-metal test programs, kernel, root filesystem
- **doom/** — DOOM port + rendering backend

---

## Build Commands (Phase 1 Preview)

```bash
# Build BOOM Verilog (requires Mill + Chisel)
make CONFIG=DoomBoomConfig verilog

# RTL Simulation with Verilator
make -C sims/verilator CONFIG=DoomBoomConfig run BINARY=tests/doom-boom/test.riscv

# (Phase 2) Generate FPGA bitstream via FireSim
firesim buildbitstream --config_build_dir sims/firesim/deploy/

# (Phase 2) Deploy to AWS F1 instance
firesim deploy

# (Phase 3) Cross-compile DOOM for RV64
riscv64-unknown-elf-gcc -O2 -march=rv64ifdc -mabi=lp64d -o doom doom.c ...
```

---

## Key Milestones & Success Criteria

| Date | Milestone | Success Criteria |
|------|-----------|------------------|
| Aug 18 | **Day 3** | CPU executes test programs in Verilator |
| Aug 22 | **Day 7** | Bootloader runs on AWS F1 FPGA |
| Aug 28 | **Day 14** | Linux shell accessible on FPGA |
| Sep 4 | **Day 21** | DOOM playable at 30+ FPS |

---

## Resources & References

- **Chipyard Docs**: `docs/` (Simulation, FPGA prototyping, VLSI)
- **BOOM**: [boom-core/boom](https://github.com/riscv-boom/riscv-boom)
- **RISC-V ISA Spec**: [riscv.org](https://riscv.org)
- **Linux RV64**: [kernel.org (RV64 support)](https://git.kernel.org)
- **Doom3 Source**: [id-software/DOOM-3](https://github.com/id-Software/DOOM-3)
- **AWS F1 FPGA**: [aws.amazon.com/ec2/instance-types/f1/](https://aws.amazon.com/ec2/instance-types/f1/)

---

## Notes & Conventions

- **Branch**: `doom-challenge-phase1` (tracking `origin/main`)
- **Daily Standup**: End-of-day status in chat + decisions.md update
- **Commits**: Clear commit messages with rationale (not just "what")
- **Escalation**: Report blockers immediately, don't work around them silently
- **Time Box**: 21 days fixed; prioritize critical path (synthesis, kernel bring-up)

---

**Last Updated**: Aug 16, 2026 | **Owner**: ninadjangle | **Status**: Phase 1 In Progress
