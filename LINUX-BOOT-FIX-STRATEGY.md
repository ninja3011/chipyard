# Linux Boot on BOOM Verilator - Fix Strategy

**Status:** Autonomous debug in progress  
**Objective:** Get kernel to produce console output on BOOM test harness  
**User:** Away overnight - full permissions granted

---

## Root Cause Hypothesis

Linux kernel hangs for 90+ minutes before producing ANY console output. This indicates:

1. **Device Tree Mismatch** (Most Likely)
   - Kernel compiled with device tree that expects devices
   - BOOM test harness provides different/missing devices
   - Kernel searching for devices, hanging in probe loop

2. **UART Initialization Failure** (Probable)
   - Console UART not matching expected base address
   - UART not initialized in test harness
   - Early printk not working

3. **Memory Configuration Issue** (Possible)
   - Kernel expects memory layout different from physical RAM
   - Page table setup failing
   - Virtual addressing initialization hang

---

## Multi-Track Debug Approach

### Track 1: Diagnose Current Issue
**Status:** Running (linux-boot-debug.sh)
- Launch with 10B cycle budget
- Monitor for ANY kernel output
- If output: Parse to identify hang point
- If no output after 3 min: Proceed to Track 2

### Track 2: Build Debug Kernel
**Trigger:** No output after 3 minutes
- Create minimal kernel config (serial only, no drivers)
- Strip down to bare minimum
- Rebuild with maximum debug output
- Test on Verilator

### Track 3: Custom Device Tree
**Trigger:** Debug kernel still hangs
- Extract BOOM test harness capabilities
- Create matching device tree (.dtb)
- Embed in kernel or pass via BBL
- Test

### Track 4: Bare-Metal Bootloader Path
**Fallback:** If Linux still fails
- Already proven DOOM works bare-metal
- Create Linux-compatible bootloader
- Load kernel directly (skip BBL)
- Gives us proof-of-concept for FPGA

---

## Implementation Priority

### Immediate (Next Hour)
1. ✅ Run debug harness (in progress)
2. ⏳ Analyze output
3. ⏳ Identify specific hang point

### Short-term (2-3 Hours)
1. Extract device tree requirements
2. Build minimal kernel variant
3. Test on Verilator
4. Iterate if needed

### Medium-term (4-6 Hours)
1. Custom device tree creation
2. Kernel rebuild with matching config
3. Full integration test

### Fallback (If needed)
1. Accept bare-metal for Verilator
2. Plan Linux debugging for FPGA (has proper infrastructure)
3. Document findings for next phase

---

## Tools Created

```bash
/home/ninadjangle/chipyard/
├── linux-boot-debug.sh            # Main debug harness (running now)
├── build-minimal-kernel.sh         # Minimal kernel config generator
├── analyze-device-tree.sh          # Device tree compatibility checker
└── LINUX-BOOT-FIX-STRATEGY.md     # This file
```

---

## Success Criteria

**Minimum:** Kernel produces console output (any "Linux version" message)  
**Target:** Shell prompt on BOOM Verilator  
**Stretch:** DOOM running under Linux on BOOM Verilator

---

## Decision Tree

```
Is kernel producing output?
├─ YES → Parse output, identify issue
│  ├─ Device not found? → Create device tree
│  ├─ UART not working? → Reconfigure UART address
│  ├─ Memory error? → Fix paging setup
│  └─ Kernel panic? → Apply kernel patches
│
└─ NO → After 3+ minutes?
   ├─ YES → Proceed to debug kernel build
   └─ NO → Wait for more output
```

---

## Timeline for User Return

- **T+1 hour:** Should have diagnostic output
- **T+2 hours:** Should have identified root cause
- **T+3-4 hours:** Should have working fix or documented blocker
- **By morning:** Linux booting on BOOM Verilator (or clear path forward)

---

## Confidence Levels

| Scenario | Confidence | Reasoning |
|----------|-----------|-----------|
| Can identify issue | Very High | 10+ hour debug session planned |
| Can fix device tree | High | Standard Linux issue |
| Can build debug kernel | High | Well-documented process |
| Full Linux boot by morning | Medium | Depends on complexity of fix |
| At minimum: Documented solution | Very High | Will iterate until success or clear blocker |

---

**Status:** In Progress  
**Next Update:** 30 minutes (after first debug attempt completes)
