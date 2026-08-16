# Days 1-7 Task Breakdown

## Day 1 (Aug 16) - Project Setup [TODAY]
**Status**: In Progress

### Architecture & Documentation
- [x] Create feature branch `doom-challenge-phase1`
- [x] Write CLAUDE.md (architecture decisions, design rationale)
- [x] Write DOOM-PROJECT.md (21-day timeline, phase breakdown)
- [x] Create this task file (tasks-day1-to-7.md)
- [ ] Create decisions.md log (ADR format)

### Build Environment Verification
- [ ] Activate conda environment: `source scripts/conda/conda-env-activate.sh env`
- [ ] Verify Mill compiles without error: `mill generators.doom-boom.compile` (TBD)
- [ ] Verify Verilator simulator environment: `make -C sims/verilator CONFIG=DefaultConfig test`
- [ ] Confirm no breaking changes to existing configs

### AWS & FPGA Access Verification (Phase 2 readiness)
- [ ] Check AWS credentials: `cat ~/.aws/credentials | head -2`
- [ ] Verify EC2 F1 instance availability: `aws ec2 describe-instance-types --filters "Name=instance-type,Values=f1*" --region us-west-2`
- [ ] Review FireSim docs: Check `sims/firesim/README.md`
- [ ] Verify FireSim config exists: `ls -la sims/firesim/deploy/config_build.ini`
- [ ] Document AWS blockers (quota, billing, region) in CLAUDE.md

### Baseline Chisel Code Structure (generators/doom-boom/)
- [ ] Create directory: `generators/doom-boom/{src/main/scala,...}`
- [ ] Write DoomBoomConfigs.scala (BOOM MediumConfig instantiation)
- [ ] Write Top.scala (module instantiation)
- [ ] Create tests/ directory structure

### Git & Final Commit
- [ ] Review all changes: `git status`
- [ ] Stage files: `git add -A`
- [ ] Create Day 1 commit with clear message
- [ ] Verify branch is clean: `git log --oneline -5`

**Owner**: ninadjangle  
**Estimated Time**: 5–6 hours  
**Blockers**: None expected (build env pre-configured)

---

## Day 2 (Aug 17) - RTL Simulation Setup
**Status**: Pending (awaits Day 1 completion)

### BOOM Configuration Finalization
- [ ] Review existing BOOM configs in `generators/boom/`
- [ ] Finalize MediumBOOMConfig parameters:
  - Fetch width: 2
  - Decode width: 2
  - Issue width: 4 (IQ width)
  - ROB entries: 64
  - Physical registers: 100 int, 64 FP
  - L1-I: 32KB (1-way), L1-D: 32KB (2-way), L2: 256KB (4-way)
- [ ] Confirm ISA support: RV64IMAFDv (all extensions present)
- [ ] Generate RTL: `make CONFIG=DoomBoomConfig verilog`
- [ ] Verify Verilog output: Check `output/DoomBoomConfig.v` ~200K lines

### Verilator Simulator Environment
- [ ] Build Verilator RTL simulator: `make -C sims/verilator CONFIG=DoomBoomConfig`
- [ ] Run existing test: `make -C sims/verilator CONFIG=DoomBoomConfig run BINARY=<existing>`
- [ ] Document any synthesis warnings (acceptable: clock timing, unused signals)

### Test Program: Factorial
- [ ] Write bare-metal factorial program in RISC-V assembly + C
- [ ] Compute 5! = 120 (simple correctness check)
- [ ] Cross-compile: `riscv64-unknown-elf-gcc factorial.c -o factorial.riscv`
- [ ] Simulate: `make -C sims/verilator CONFIG=DoomBoomConfig run BINARY=factorial.riscv`
- [ ] Verify output (via UART log): "120" or register inspection

### Documentation & Commit
- [ ] Update decisions.md: ADR-003 (BOOM config choices)
- [ ] Document simulation results in this file
- [ ] Commit changes: clear message, link to Day 1

**Owner**: ninadjangle  
**Dependencies**: Day 1 complete ✓  
**Estimated Time**: 6–8 hours (Verilator build can be slow)  
**Blockers**: None anticipated

---

## Day 3 (Aug 18) - Bootloader + Hello World
**Status**: Pending (awaits Day 2 completion)

### Minimal Bootloader
- [ ] Write boot ROM (8KB max) in RISC-V assembly:
  - Initialize stack pointer (SP = 0x80000000 + 8KB)
  - Set exception handler address
  - Enable UART output (write to MMIO)
  - Load kernel from external memory (or test program)
  - Jump to entry point
- [ ] Cross-compile: `riscv64-unknown-elf-as bootloader.s -o bootloader.o`
- [ ] Verify: Bootloader runs, initializes UART, prints "Boot successful"

### Hello-World Bare-Metal App
- [ ] Write simple app that bootloader can jump to:
  - Print "Hello from DOOM CPU!"
  - Compute Fibonacci(10) and print result
  - Halt cleanly
- [ ] Cross-compile: `riscv64-unknown-elf-gcc -nostdlib hello.c -o hello.riscv`

### Integration Testing
- [ ] Modify RTL testbench to:
  - Load bootloader into ROM
  - Load hello app into RAM
  - Run simulation to completion
- [ ] Verify output via Verilator UART mock:
  - "Boot successful" message appears
  - "Hello from DOOM CPU!" message appears
  - Fibonacci result printed
- [ ] Capture console output: Save to `results/day3-boot-output.log`

### Code Quality & Documentation
- [ ] Document bootloader flow (comments in asm)
- [ ] Document MMIO register layout for UART, timer
- [ ] Commit bootloader + hello-world: Clear message linking to Day 2

**Owner**: ninadjangle  
**Dependencies**: Day 2 Verilator + test program working ✓  
**Estimated Time**: 5–6 hours (bootloader fiddly, iterative debugging)  
**Blockers**: UART mock in Verilator may need custom code

---

## Days 4-7 - FireSim + AWS Deployment
**Status**: Pending (awaits Day 3 completion)

### Day 4-5: FireSim Configuration
- [ ] Review FireSim documentation: `sims/firesim/docs/index.md`
- [ ] Configure FireSim for AWS F1:
  - Set region, instance type (f1.2xlarge target)
  - Configure tool paths (Vivado, Modelsim)
  - Set bitstream output location
- [ ] Prepare network config (FPGA ↔ host communication)
- [ ] Test FireSim on local machine: `firesim --help`

### Day 5-6: Bitstream Generation
- [ ] Generate FPGA bitstream via Vivado (4–6 hour synthesis):
  - `firesim buildbitstream --config_build_dir sims/firesim/deploy/`
  - Monitor synthesis: Check timing, utilization reports
  - Expected: ~80% LUTs, ~70% BRAMs, timing closure
- [ ] Post-synthesis verification: Check bitstream size (<250MB)
- [ ] Archive bitstream: Save to S3 or backup

### Day 6-7: AWS F1 Deployment & Testing
- [ ] Provision AWS EC2 F1 instance:
  - Request instance quota if needed
  - Create t3.large for control, f1.2xlarge for FPGA
  - Configure security group, IAM role
- [ ] Deploy bitstream to FPGA:
  - `firesim deploy --config <config>`
  - Load bitstream via JTAG/EFT
- [ ] Test bootloader on real FPGA:
  - Connect UART console
  - Run bootloader, verify "Boot successful"
  - Document any timing issues, signal integrity problems

### Commit & Daily Standup
- [ ] Commit FireSim config changes
- [ ] Update decisions.md: AWS deployment decisions
- [ ] Update this file with results
- [ ] Day 7 standup: Ready for Linux kernel bring-up? (Go/No-Go decision)

**Owner**: ninadjangle  
**Dependencies**: Day 3 bootloader verified ✓  
**Estimated Time**: 12–16 hours (synthesis, deployment, debugging)  
**Blockers**: AWS quota, Vivado license, synthesis failure recovery

---

## Success Criteria & Rollforward Plan

| Day | Blocker Scenario | Rollforward Action |
|-----|------------------|-------------------|
| 2 | Verilator build fails | Use VCS/Xcelium fallback, or debug Mill config |
| 3 | Bootloader hangs | Add assembly debug output, single-step through RTL |
| 5 | Synthesis fails (timing) | Reduce L2 size from 256KB to 128KB, or switch to SmallBOOMConfig |
| 6 | AWS quota issue | Request increase immediately, use local dev machine fallback |
| 7 | FPGA bitstream wrong size | Optimize design constraints, reduce frequency if needed |

---

## Notes & Assumptions

- **Build environment is pre-configured**: Conda, RISC-V toolchain, Mill, Chisel all working
- **Verilator compile time**: ~30–60 min first time, then incremental
- **Synthesis time dominates Days 4–5**: Plan accordingly (parallel builds, sleep schedule)
- **AWS F1 pricing**: ~$2–5 per hour for development; budget ~$100 for full project
- **No external dependencies**: Chipyard is self-contained; no additional repos needed

---

**Last Updated**: Aug 16, 2026 | **Owner**: ninadjangle | **Status**: Approved
