# DOOM on RISC-V Challenge - 21 Day Project

## Project Summary
Build a fully functional RISC-V CPU (BOOM-based) running on AWS FPGA, capable of executing Doom3 or a modern DOOM port. Span from RTL design → bootloader → Linux kernel → interactive game. Proof-of-concept for custom processor + full software stack in 21 days.

## Architecture & Design Decisions

### CPU Design: BOOM MediumConfig
- **Base Processor**: Berkeley Out-of-Order Machine (BOOM) v3
- **Pipeline Configuration**:
  - **Issue width**: 4 instructions/cycle
  - **Pipeline depth**: 8 stages
  - **ROB entries**: 64
  - **Physical registers**: 100 integer, 64 FP
- **ISA**: RV64IMAFDv
  - RV64I: Base 64-bit RISC-V
  - M: Integer multiply/divide
  - A: Atomic operations (needed for Linux SMP if applicable)
  - F/D: Floating-point single/double (Doom3 rendering math)
  - v: Vector extension (stretch goal, may defer)
- **Cache Hierarchy**:
  - L1-I: 32KB, direct-mapped (64 sets × 512B lines)
  - L1-D: 32KB, 2-way associative (64 sets × 512B lines)
  - L2: 256KB, 4-way associative (512 sets × 512B lines)
  - **Rationale**: Conservative memory footprint; can optimize if FPGA resource-constrained
- **Memory Subsystem**:
  - 64-bit external memory bus
  - **Physical memory**: 2GB (start with 512MB simulation, scale to 2GB on FPGA)
  - **Memory layout**:
    - 0x00000000–0x00001000: Exception handlers + interrupt vectors
    - 0x00001000–0x01000000: User space (stack + heap)
    - 0x80000000–0x80100000: Kernel boot ROM + device tree
    - 0xFFFF000000000000: Kernel virtual space (Sv48 paging)

### Memory Map & Boot Sequence
1. **Boot ROM** (first 8KB): Minimal bootloader
   - Initialize UART, exception handler
   - Load kernel from external storage
   - Jump to kernel entry point
2. **Device Tree**: Describe CPU, UART, memory, interrupts
3. **Kernel**: Linux RV64 with minimal drivers (UART, timer, paging)
4. **User Space**: Doom3 or DOOM port binary + shared libraries

### Toolchain & Build System
- **Chisel Version**: 6.7.0 (latest stable in Chipyard)
- **Build Tool**: Mill (SBT-based) for Chisel compilation
- **Simulation**: Verilator (C++ RTL simulator, fastest iteration)
- **Synthesis**: Vivado 2023.x (via Chipyard's VLSI flow)
- **FPGA Deployment**: FireSim (Chipyard's integrated tool for AWS F1)
- **RISC-V Toolchain**: riscv64-unknown-elf-* (gnu-toolchain)
  - gcc, gdb, objdump, spike (ISA simulator)
- **Linux Kernel**: RV64 mainline (post-5.x)
- **Root Filesystem**: buildroot or Chipyard's FireMarshal

### Software Stack (Phase Breakdown)

**Phase 1 (Days 1-3): Foundation**
- BOOM CPU RTL finalization
- Bootloader in assembly (set SP, exception handler, UART)
- Simple bare-metal test programs (fibonacci, memory test, LED blink simulation)

**Phase 2 (Days 4-14): FPGA & Kernel**
- FireSim bitstream generation (4–6 hours)
- AWS F1 instance setup and synthesis verification
- Linux kernel RV64 bring-up (device drivers for UART, timer, page table setup)
- Root filesystem with minimal utilities (sh, ls, cat, dd)

**Phase 3 (Days 15-21): Doom3 Port & Optimization**
- Identify DOOM port (original Doom, Doom3, or lightweight variant)
- Cross-compile with RISC-V toolchain
- Create custom graphics backend (framebuffer writer)
- Optimize CPU and cache for rendering workload
- Final integration testing on FPGA

### Critical Constraints & Risks

**Resource Constraints**:
- FPGA Bitstream size: ~180–220MB for BOOM (tight; AWS F1 limit is ~250MB)
- Synthesis time: 4–6 hours per full iteration (critical path bottleneck)
- DRAM bandwidth: 256 bits @ system clock (adequate for Doom3 at 60 FPS target)
- Power consumption: AWS F1 thermal limits (design for <100W target)

**Timeline Risk**:
- 21 days is aggressive; no slip tolerance
- Synthesis time may block critical path (parallelize if possible)
- Doom3 porting scope can creep (use minimal port or custom renderer fallback)
- AWS quota/billing limits may block deployment (verify Day 1)

**Technical Debt Acceptance**:
- Will NOT implement full RISC-V privilege levels initially; use M-mode only
- Will NOT optimize for power/area in Phase 1; focus on correctness
- Will defer vector extension (RVv) to post-21-day optimization

### Success Criteria & Milestones

| Day | Milestone | Success Criteria |
|-----|-----------|------------------|
| 1   | Architecture finalized | CLAUDE.md + BOOM config approved |
| 2–3 | RTL simulation proven | CPU executes factorial/fibonacci w/o error |
| 4–7 | FPGA deployment working | Bitstream synthesizes + AWS F1 runs bootloader |
| 8–14 | Linux kernel boots | Shell prompt accessible via UART |
| 15–18 | DOOM porting complete | DOOM renders framebuffer correctly |
| 19–21 | Optimization & final test | DOOM playable (30+ FPS target) on FPGA |

### Dependencies & External Requirements

1. **Doom3 or DOOM Source**:
   - Licensing (GPL, ID Tech, etc.) - verify before Phase 3
   - Expected to find RV64 port or adapt lightweight variant
   - Fallback: Custom DOOM renderer in C (triangle rasterizer)

2. **AWS Resources**:
   - F1 instance access (verify quota on Day 1)
   - Vivado license (AWS provides via FPGA developer AMI)
   - ~$50–100 estimated cost for synthesis + 2–3 days FPGA runtime

3. **Build Environment**:
   - Conda environment with Chisel, RISC-V toolchain, Vivado (pre-configured)
   - Linux kernel source (mainline RV64 support)
   - buildroot for filesystem generation

### Communication & Daily Standup Protocol

- **Format**: End-of-day status in this chat + decisions log
- **Decision Log** (decisions.md): Architecture decisions with rationale + status
- **Commit Messages**: Clear "why" (not just "what") for design choices
- **Blockers**: Escalate immediately—don't silently pivot around them
- **Code Review**: Each major commit gets a rationale in the message

### Known Fallback Plans

| Blocker | Fallback |
|---------|----------|
| BOOM resource exhaustion | Switch to SmallBOOMConfig (2-wide, 6-stage) or Rocket core |
| Vivado license unavailable | Use open-source FPGA toolchain (Project Trellis/yosys) or defer FPGA |
| Doom3 porting blocked | Implement custom minimal DOOM renderer in C (100–200 LOC) |
| Linux kernel bring-up fails | Boot baremetal Doom3 directly (skip OS layer) |
| AWS quota exceeded | Synthesize locally on dev machine (slower, but possible) |

---

## File Structure

```
chipyard/
├── CLAUDE.md                           # This file (architecture decision log)
├── decisions.md                        # ADR log (decision rationale + status)
├── README.md                           # Quick start + 21-day timeline
├── tasks-day1-to-7.md                 # Detailed task breakdown
├── generators/
│   └── doom-boom/                      # Custom BOOM CPU config
│       ├── DoomBoomConfigs.scala       # BOOM MediumConfig instantiation
│       ├── Top.scala                   # Top-level module
│       └── tests/                      # Unit test structure
├── bootloader/                         # Minimal boot ROM (Phase 1)
├── software/
│   ├── baremetal/                      # Test programs (Phase 1)
│   └── kernel/                         # Linux kernel config + patches (Phase 2)
└── doom/                               # Doom3 port + rendering backend (Phase 3)
```

## Next Steps (Day 2)

1. Finalize BOOM MediumConfig in Chisel (confirm ISA support)
2. Build Verilator RTL simulator
3. Write factorial test program (proof of execution)
4. Simulate to completion (no crashes, correct output)
5. Daily standup: document results

---

**Created**: Aug 16, 2026 | **Status**: APPROVED | **Owner**: ninadjangle
