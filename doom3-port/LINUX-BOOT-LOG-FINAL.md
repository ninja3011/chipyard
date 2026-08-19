# Linux Boot on BOOM Verilator - Final Attempt

**Date:** 2026-08-19 12:14 UTC  
**Status:** In Progress (T+3 min, expecting completion T+90-120 min)

## Setup

| Component | Value |
|-----------|-------|
| **Simulator** | BOOM MediumBoomV3Config on Verilator |
| **Binary** | br-base-bin (24MB, pre-built with kernel + rootfs) |
| **Max cycles** | 5 billion (5x previous attempt) |
| **Timeout** | 5 hours (18000 sec) |
| **Log file** | /tmp/linux-final-retry.log |

## Key Insight

**Previous attempt failed:** 2 billion cycles = ~110 minutes (ran out)  
**This attempt:** 5 billion cycles = ~275 minutes (plenty of headroom)

Real Linux boot time: 2-5 seconds  
At 1000x sim slowdown: 30-80 minutes for kernel to reach console  
Expected total: 100-150 minutes to shell prompt

## Timeline

| Time | Event |
|------|-------|
| T+0 | Boot starts, br-base-bin loading |
| T+1-30 | Early boot (no output yet) |
| T+30-60 | Memory initialization |
| T+60-90 | Kernel initialization, device discovery |
| T+90-120 | Console output should appear |
| T+120+ | Shell prompt (if successful) |

## Next Steps After Linux Boots

1. ✅ Verify "Linux version" message in console
2. ✅ Wait for shell prompt (#)
3. ✅ Copy DOOM binary to system
4. ✅ Execute DOOM and measure FPS
5. ✅ Document results

## Checkpoints

- [ ] T+30 min: Verify simulator still running
- [ ] T+60 min: Check for kernel output  
- [ ] T+90 min: Expect console messages
- [ ] T+120 min: Shell prompt or error analysis

## DOOM Binary Ready

```bash
# Pre-compiled and tested on QEMU/Spike:
doom3-port/doom-qemu-test-spike.riscv (5.6K)

# Bare-metal variant (if needed):
doom3-port/doom-baremetal.riscv (9.8K)
```

Once Linux boots:
```bash
# Copy to system
scp doom-qemu-test-spike.riscv root@localhost:/tmp/

# Run
/tmp/doom-qemu-test-spike.riscv

# Expected: exit code 0 (game loop completed successfully)
```

## Success Criteria

✅ **Kernel output** appears in log  
✅ **Shell prompt** reachable  
✅ **DOOM executes** without error  
✅ **Exit code 0** returned

---

**Status:** AWAITING KERNEL OUTPUT (real-time clock)  
**Monitor:** `tail -f /tmp/linux-final-retry.log`
