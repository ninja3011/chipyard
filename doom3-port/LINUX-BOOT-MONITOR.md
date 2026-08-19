# Linux Boot Simulation Monitor

**Status:** IN PROGRESS  
**Start Time:** 2026-08-19 08:45 UTC  
**Expected Completion:** 2026-08-19 12:00 UTC (3-4 hours)  
**Process ID:** 27151

---

## Simulation Details

**Command:**
```bash
timeout 14400 sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
  +permissive \
  +max-cycles=1000000000 \
  /tmp/pk-linux
```

**Components:**
- Bootloader: RISC-V Proxy Kernel (pk)
- Kernel: Linux RV64 vmlinux (22MB)
- Simulator: BOOM Verilator (14MB executable)

**Output Log:** `/tmp/linux-sim.log`

---

## Expected Boot Sequence

### Phase 1: Bootloader (minutes 0-5)
- pk initializes M-mode environment
- Sets up trap handlers
- Initializes UART console
- Output: `[pk] ...`

### Phase 2: Kernel Load (minutes 5-30)
- pk loads vmlinux ELF sections
- pk jumps to kernel entry (0xffffffff80000000)
- Kernel initializes memory management
- Output: `Linux version ...`

### Phase 3: Kernel Boot (minutes 30-120)
- Kernel sets up exception handlers
- Kernel initializes devices (UART, timer)
- Kernel mounts root filesystem
- Kernel starts init process
- Output: `Starting init ... / # `

### Phase 4: DOOM Execution (minutes 120-240)
- Shell accepts commands
- DOOM binary executes
- Game logic runs for 100 frames
- Output: `Frame X / 100 ...` (or similar)

### Phase 5: Completion (minute 240+)
- DOOM exits with status code
- Simulation terminates
- Log shows all results

---

## Monitoring Commands

### Check if still running:
```bash
ps aux | grep "simulator.*MediumBoomV3Config" | grep -v grep
```

### View live output:
```bash
tail -f /tmp/linux-sim.log
```

### Check for kernel messages:
```bash
tail /tmp/linux-sim.log | grep -E "Linux|kernel|init|shell|#"
```

### Count simulation cycles:
```bash
tail /tmp/linux-sim.log | grep -o "\[[0-9]*000\]" | tail -1
```

---

## Success Indicators

### Kernel Boot Success
- ✅ Appears in log: "Linux version"
- ✅ Appears in log: "memory management initialized"
- ✅ Appears in log: "starting init" or "/ #"

### DOOM Execution Success
- ✅ Appears in log: "doom" or "/usr/bin/" or "executing"
- ✅ Appears in log: "Frame" or game-related output
- ✅ Appears in log: "exit" or "success"

### Overall Success
- ✅ Process exits with code 0 (or timeouts gracefully)
- ✅ Log file > 1MB (indicates significant execution)
- ✅ No kernel panic messages

---

## Fallback Actions

If simulation hangs (no output for 30 min):
1. Check `/tmp/linux-sim.log` for errors
2. Look for "kernel panic" or "CPU exception"
3. If hung, kill process: `kill 27151`
4. Document where it failed for debugging

---

## Next Steps After Boot

Once boot succeeds:
1. ✅ Document kernel boot messages
2. ✅ Extract DOOM execution logs
3. ✅ Compare with FPGA boot sequence
4. ✅ Proceed to Days 4-7 (FPGA deployment)

If boot fails:
1. ✅ Analyze failure point
2. ✅ Document blockers
3. ✅ Skip to FPGA (faster boot on real hardware)
4. ✅ Proceed to Days 4-7 with manual kernel debugging

---

## Timeline

| Milestone | Expected | Actual | Status |
|-----------|----------|--------|--------|
| Simulation start | 08:45 | 08:45 | ✅ |
| pk initialization | 08:50 | TBD | ⏳ |
| Kernel boot | 09:15 | TBD | ⏳ |
| Shell prompt | 11:30 | TBD | ⏳ |
| DOOM execution | 12:00 | TBD | ⏳ |
| Completion | 12:15 | TBD | ⏳ |

---

## Notes

- Simulation is intentionally slow (1000x slower than hardware)
- This validates the entire boot stack before FPGA deployment
- Any issues found here will appear on FPGA but with much faster diagnosis
- Full documentation will be prepared post-execution

