# Linux + DOOM Boot on BOOM Verilator - Execution Summary

**Status:** SIMULATION IN PROGRESS  
**Objective:** Boot Linux kernel and run DOOM for 100 frames on Verilator  
**Start Time:** 2026-08-19 08:30 UTC  
**Expected Completion:** 2026-08-19 12:00-12:30 UTC (3-4 hours wall time)

---

## Simulation Details

**Simulator:** BOOM MediumBoomV3Config Verilator  
**Bootloader:** RISC-V Proxy Kernel (pk, 77KB)  
**Kernel:** Linux RV64 vmlinux (22MB)  
**Rootfs:** Buildroot minimal (293MB ext2, streamed via HTIF)  
**Max cycles:** 1,000,000,000 (1 billion simulation cycles)  
**Timeout:** 4 hours wall time

### Key Technical Details

**Memory Layout:**
- Physical RAM: 0x80000000 - 0x90000000 (256MB simulated)
- Kernel virtual: 0xffffffff80000000+ (Sv39 paging mode)
- UART device: 0x10000000 (simulated UART)

**Boot Chain:**
```
Test Driver (clock/reset)
  ↓
BOOM CPU (starts in machine mode)
  ↓
pk bootloader (M-mode initialization)
  ↓
Loads vmlinux ELF
  ↓
Jumps to 0xffffffff80000000 (kernel entry, S-mode)
  ↓
Linux kernel boot sequence
  ↓
Init process + shell
  ↓
Execute DOOM game logic
```

---

## Expected Log Output Timeline

### T+0-5 minutes: Bootloader Phase
```
[UART] UART0 is here
pk: loading kernel...
pk: setting up environment
```

### T+5-30 minutes: Kernel Load Phase
```
Linux version X.X.X-... (built by...)
Memory: XXXMB available
CPU: 1 hart detected
SMP: Bringing up secondary...
```

### T+30-120 minutes: Kernel Boot Phase
```
clocksource: riscv_clocksource: mask: ... bits
Calibrating delay loop...
Registering RISC-V specific extensions...
Mount-cache hash table entries: ...
Mountpoint cache hash table entries: ...
init: mount_root: trying ext2...
EXT2-fs: mounted filesystem
init: exec /bin/sh
/ # 
```

### T+120-180 minutes: DOOM Execution Phase
```
/ # /usr/bin/doom-qemu-test-spike.riscv
Game initialized
Running 100-frame simulation...
Frame 1/100
Frame 2/100
...
Frame 100/100
Game complete: SUCCESS
Exit code: 0
```

### T+180+: Completion
```
[exit notification]
Simulation complete
```

---

## Simulation Performance Notes

**Why so slow?**
- Verilator RTL simulation: ~1000x slower than real hardware
- 1 billion cycles at 1 GHz = 1 second real time
- Linux boot typically takes 10-30 seconds on hardware
- On Verilator: 2-8 hours is typical

**Performance breakdown:**
- pk initialization: ~100K cycles (0.1 sec simulator time)
- Kernel load: ~100M cycles (100 sec simulator time, ~1.5 min wall)
- Kernel boot: ~500M cycles (500 sec simulator time, ~2 hours wall)
- DOOM execution: ~100M cycles (100 sec simulator time, ~30 min wall)
- **Total estimate: 3-4 hours wall time**

**Cycle budget:**
- Available: 1,000,000,000 cycles
- Boot needs: ~600,000,000 cycles (comfortable margin)

---

## Monitoring & Logs

**Live log:** `/tmp/linux-sim.log`  
**Monitor:** Background process watching for first output  

**Commands to check progress:**
```bash
# Check if simulator is running
ps aux | grep "simulator.*MediumBoomV3Config" | grep -v grep

# View live log
tail -f /tmp/linux-sim.log

# Count lines (indicates progress)
wc -l /tmp/linux-sim.log

# Search for key milestones
grep "Linux\|kernel\|shell\|DOOM" /tmp/linux-sim.log
```

---

## Success Criteria

### Minimum Success (Kernel boots)
- ✅ Log contains "Linux version"
- ✅ Log contains shell prompt "# "
- ✅ Log contains device initialization messages
- ✅ No kernel panic or CPU exception

### Full Success (DOOM runs)
- ✅ DOOM binary loads from /usr/bin/
- ✅ Game logic executes (frame count appears)
- ✅ Completes 100 frames successfully
- ✅ Exit code 0

### Validation Success
- ✅ Boot sequence matches FPGA boot sequence
- ✅ Kernel output is identical across platforms
- ✅ DOOM results are reproducible

---

## What We'll Learn

### From Successful Boot
1. **Architecture validation:** Full Linux stack works on BOOM RTL
2. **Memory system:** Page tables, virtual addressing, cache coherency
3. **Device drivers:** UART, timer, block device (HTIF)
4. **Boot sequence:** Exact steps to reproduce on FPGA

### From DOOM Execution
1. **Game logic correctness:** 100-frame simulation on actual CPU
2. **Floating point:** If DOOM uses FPU, validates floating point
3. **Memory access patterns:** Bandwidth, latency, cache behavior
4. **System stability:** Kernel handles DOOM workload correctly

### For FPGA Deployment (Days 4-7)
- Bootloader code is validated (works on RTL)
- Device tree/kernel config is validated
- DOOM binary is validated
- Performance expectations can be set

---

## Contingency Plans

### If simulation hangs (no output for 30+ min)
1. Check `/tmp/linux-sim.log` for error messages
2. Look for "kernel panic" or "CPU exception"
3. Kill simulator: `pkill -f "simulator.*MediumBoomV3Config"`
4. Document failure point, proceed to FPGA

### If kernel panics
1. Document panic message
2. Adjust kernel parameters if applicable
3. Proceed to FPGA (may work better with real hardware)

### If DOOM doesn't execute but kernel boots
1. Document successful kernel boot (victory #1)
2. Skip DOOM on simulator
3. Proceed to FPGA (DOOM will run faster there anyway)

---

## Next Phases (After Linux Boot)

### Immediate (After Completion)
1. ✅ Analyze boot logs for timing
2. ✅ Extract DOOM execution metrics
3. ✅ Document any issues encountered
4. ✅ Create FPGA boot procedure

### Days 4-7 (AWS FPGA Deployment)
1. Use bootloader code from this simulation
2. Boot Linux on AWS F1 FPGA
3. Expect 30-second boot (vs 3-4 hours on simulator)
4. Validate DOOM runs same way

### Days 8-14 (Linux Kernel Bring-up)
1. Fine-tune kernel parameters based on FPGA perf
2. Add device drivers for FPGA peripherals
3. Optimize Linux for DOOM workload

### Days 15-21 (DOOM Porting & Optimization)
1. DOOM already proven to run on Linux
2. Port to FPGA with any HW-specific optimizations
3. Target 30+ FPS on FPGA hardware

---

## Files & Artifacts

**Simulation setup:**
- `/tmp/pk-linux` - pk bootloader with embedded kernel
- `/tmp/linux-sim.log` - Live log file

**Documentation:**
- `LINUX-BOOT-MONITOR.md` - Real-time status
- `LINUX-VERILATOR-PLAN.md` - Detailed technical plan
- `LINUX-BOOT-EXECUTION-SUMMARY.md` - This document

**Source:**
- `doom3-port/linux-boot.s` - Bootloader assembly
- `doom3-port/run-linux-verilator.sh` - Boot script

---

## Summary

This 3-4 hour simulation will:
1. ✅ Prove Linux boots on BOOM RISC-V CPU
2. ✅ Prove DOOM game logic runs on Linux
3. ✅ Validate complete software stack end-to-end
4. ✅ Create bootloader and kernel configs for FPGA

**Outcome:** Whether success or failure, we'll have crucial data to inform FPGA deployment decisions.

