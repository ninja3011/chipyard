# Days 4-7: AWS FPGA Deployment Plan

**Status:** READY TO START  
**Timeline:** August 20-23, 2026 (4 days)  
**Objective:** Synthesize BOOM to FPGA bitstream, deploy to AWS F1, boot Linux

---

## What We're Deploying

**Verified Components:**
- ✅ BOOM MediumBoomV3Config (4-wide, 8-stage pipeline)
- ✅ RISC-V ISA (proven on QEMU, Spike, BOOM RTL)
- ✅ DOOM game logic (proven to run)
- ✅ Linux kernel (22MB vmlinux, RV64)
- ✅ Buildroot rootfs (minimal Linux system)

**Deployment Tool:**
- FireSim (Chipyard's integrated FPGA tool)
- Handles: Bitstream generation, AWS integration, simulation→hardware

---

## Phase 1: Local Synthesis Preparation (Day 4 morning)

### Step 1: Verify FPGA Tools
```bash
# Check Vivado availability
vivado -version

# Check FireSim configuration
cd chipyard/software/firesim
cat deploy/config_build.ini
```

### Step 2: Configure FireSim for BOOM
```
Update config_build.ini:
- target_config: MediumBoomV3Config
- host_f1_instance: f1.2xlarge (or suitable size)
- num_fpgas: 1 (single FPGA)
- synthesis_medium: true (for faster builds)
```

### Step 3: Generate RTL (if not cached)
```bash
cd chipyard
make -C sims/verilator CONFIG=MediumBoomV3Config verilog
# Creates: sims/verilator/generated-src/.../...v
```

### Step 4: Run FireSim Build
```bash
cd software/firesim
firesim buildafull
# Estimated time: 4-6 hours (synthesis, place & route)
```

---

## Phase 2: AWS FPGA Deployment (Day 4-5)

### Prerequisites
- AWS account with F1 access
- AWS credentials configured
- ~$50-100 budget allocated

### Step 1: Launch AWS Environment
```bash
cd firesim
firesim launchrunfarm

# This:
# - Creates EC2 instance
# - Installs Vivado license server
# - Stages bitstream for programming
```

### Step 2: Program FPGA
```bash
firesim runfarm

# Loads bitstream to F1 FPGA
# Expected time: 5-10 minutes
```

### Step 3: Verify Boot
```bash
# Connect to running instance
ssh -i keyfile ubuntu@instance_ip

# Check UART console
screen /dev/ttyUSB0 115200  # or equivalent

# Expected output:
# [FPGA] Linux version ...
# [FPGA] Booting ...
```

---

## Phase 3: Linux Bring-up (Day 5-7)

### Day 5: Basic Linux Boot
**Goals:**
- [ ] Kernel boots to shell prompt
- [ ] UART console responds
- [ ] Can type commands
- [ ] Rootfs mounts correctly

**Commands:**
```bash
# From shell:
ls /           # List root directory
cat /proc/version  # Verify kernel version
dmesg          # View boot messages
```

### Day 6: Device Support
**Goals:**
- [ ] FPGA-specific devices detected
- [ ] Memory available (how much?)
- [ ] Timer interrupt working
- [ ] Ethernet (if applicable)

**Testing:**
```bash
# Check available memory
free -h

# List devices
cat /proc/devices

# Check loaded modules
lsmod

# Run network test (if applicable)
ping 8.8.8.8
```

### Day 7: DOOM Execution
**Goals:**
- [ ] Copy DOOM binary to FPGA
- [ ] Execute DOOM for 100 frames
- [ ] Measure FPS performance
- [ ] Validate results match simulator

**Commands:**
```bash
# Upload DOOM binary (from host)
scp doom-qemu-test-spike.riscv ubuntu@instance:/tmp/

# On FPGA:
/tmp/doom-qemu-test-spike.riscv

# Expected: Game logic runs, exit code 0
# Performance: Should be much faster than Verilator
```

---

## Success Criteria by Day

### Day 4 (Synthesis)
- ✅ Bitstream generates successfully
- ✅ Placed & routed without errors
- ✅ < 6 hours total build time
- ✅ File size < 250MB

### Day 5 (Deployment)
- ✅ FPGA programmed
- ✅ UART shows kernel boot messages
- ✅ Shell prompt appears
- ✅ Kernel version matches expected

### Day 6 (Linux Integration)
- ✅ Rootfs mounts
- ✅ Standard utilities work (ls, cat, etc.)
- ✅ Memory system functional
- ✅ No kernel panics

### Day 7 (DOOM Validation)
- ✅ DOOM binary executes
- ✅ Game logic completes 100 frames
- ✅ Exit code matches simulator
- ✅ FPS performance recorded

---

## Risk Mitigation

### If Synthesis Fails
- **Issue:** RTL has linting errors
- **Fix:** Check generated-src logs, address Chisel errors
- **Fallback:** Use SimpleConfig instead of MediumBoomV3Config
- **Effort:** +2 hours

### If FPGA Programming Fails
- **Issue:** Bitstream incompatible with F1 device
- **Fix:** Verify Vivado version, re-synthesize
- **Fallback:** Try different F1 instance type
- **Effort:** +1-2 hours

### If Linux Doesn't Boot
- **Issue:** Kernel panic on FPGA (different from simulator)
- **Diagnosis:** Check UART output, kernel messages
- **Fix:** Adjust device tree, kernel parameters
- **Effort:** +2-4 hours

### If DOOM Doesn't Run
- **Issue:** Executable format, missing libraries
- **Diagnosis:** Run with strace, check ldd
- **Fix:** Recompile DOOM for FPGA environment
- **Effort:** +2-3 hours
- **Fallback:** Already proven on QEMU/simulator, so library issue not architectural

---

## Cost Estimate

| Component | Hours | Cost |
|-----------|-------|------|
| Synthesis | 6 | $0 (local) |
| AWS F1 instance | 8 | $30-40 |
| Vivado license | included | $0 |
| Total | 14 | $30-40 |

Budget: $100 (comfortable margin)

---

## Knowledge Gained from Days 1-3

**What this enables:**
1. ✅ Proven RISC-V ISA works (3 platforms)
2. ✅ DOOM game logic proven
3. ✅ BOOM architecture validated
4. ✅ Bootloader strategy known
5. ✅ Linux kernel verified to compile

**What we'll learn in Days 4-7:**
1. BOOM on real hardware (vs simulation)
2. FPGA-Linux integration
3. Performance vs simulator (should be ~1000x faster)
4. Any hardware-specific issues

---

## Post-FPGA Readiness (Days 8+)

Once Days 4-7 complete:
- ✅ Full stack runs on FPGA (bootloader → kernel → DOOM)
- ✅ Performance baseline established
- ✅ Hardware-software integration proven
- ✅ Ready for optimization phase (Days 8-21)

**Days 8-14:** Linux kernel optimization  
**Days 15-21:** DOOM porting & graphics pipeline

---

## Timeline Summary

```
Aug 20  Day 4: Synthesis (6 hrs) + AWS setup (2 hrs)
Aug 21  Day 5: Programming (1 hr) + Linux boot (5 hrs)
Aug 22  Day 6: Device bring-up (8 hrs)
Aug 23  Day 7: DOOM validation + optimization (8 hrs)

Aug 24-30  Days 8-14: Linux tuning + drivers
Aug 31-Sep 6  Days 15-21: DOOM porting + final integration
```

---

## Files to Reference

**Configuration:**
- `CLAUDE.md` - Architecture decisions
- `doom3-port/QEMU-*.md` - Previous verification results
- `doom3-port/linux-boot.s` - Bootloader assembly

**Binaries:**
- `software/firemarshal/boards/default/linux/vmlinux` - Kernel
- `doom3-port/doom-qemu-test-spike.riscv` - DOOM game logic

**Scripts:**
- (Will create) `run-fpga-linux-boot.sh` - Boot script
- (Will create) `upload-doom-to-fpga.sh` - Transfer & run

---

## Ready to Proceed

All prerequisites met. Standing by for Day 4 startup.

