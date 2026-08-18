# DOOM on RISC-V - Comprehensive Performance Metrics

**Test Date:** 2026-08-18  
**Platform:** BOOM MediumV3Config (RV64IMAFDv)  
**Simulator:** Verilator RTL (Cycle-Accurate)  
**Test Binary:** doom.riscv (100 KB)

---

## 1. Performance Metrics

### Frame Rendering
- **Target FPS:** 35
- **Achieved FPS:** TBD
- **Frame Time (ms):** TBD
- **Frame Time Variance:** TBD
- **Minimum Frame Time:** TBD
- **Maximum Frame Time:** TBD
- **99th Percentile:** TBD

### Workload Breakdown
- **Total Simulation Time:** TBD
- **Total Cycles:** TBD
- **Test Duration:** TBD

---

## 2. CPU Utilization Metrics

### Pipeline Efficiency
- **Instructions Per Cycle (IPC):** TBD
- **Stalls per 1000 cycles:** TBD
  - Load/Store Unit Stalls: TBD %
  - Branch Misprediction Stalls: TBD %
  - Cache Miss Stalls: TBD %
  - Dependency Stalls: TBD %

### Issue Width Analysis
- **Average Issue Width:** TBD instructions/cycle
- **Cycles with 4 issues:** TBD %
- **Cycles with 3 issues:** TBD %
- **Cycles with 2 issues:** TBD %
- **Cycles with 1 issue:** TBD %
- **Cycles with 0 issues:** TBD %

### Instruction Mix
- **ALU Operations:** TBD %
- **Load Operations:** TBD %
- **Store Operations:** TBD %
- **Branch Operations:** TBD %
- **Floating Point:** TBD %

---

## 3. Memory Subsystem Metrics

### L1 Instruction Cache
- **Total Accesses:** TBD
- **Hit Rate:** TBD %
- **Miss Rate:** TBD %
- **Miss Penalty (cycles):** TBD

### L1 Data Cache
- **Total Accesses:** TBD
- **Load Hit Rate:** TBD %
- **Store Hit Rate:** TBD %
- **Overall Hit Rate:** TBD %
- **Miss Penalty (cycles):** TBD

### L2 Cache
- **Total Accesses:** TBD
- **Hit Rate:** TBD %
- **Miss Rate:** TBD %
- **Access Latency:** TBD cycles
- **Write-back Rate:** TBD

### Memory Bandwidth
- **Peak Bandwidth:** 256 bits @ clock frequency
- **Achieved Bandwidth:** TBD GB/s
- **Bandwidth Saturation:** TBD %
- **Average Access Latency:** TBD cycles

---

## 4. Branch Prediction Metrics

### Branch Statistics
- **Total Branches:** TBD
- **Correct Predictions:** TBD %
- **Mispredictions:** TBD %
- **Misprediction Penalty:** TBD cycles average

### Branch Types
- **Conditional Branches:** TBD %
- **Unconditional Jumps:** TBD %
- **Calls/Returns:** TBD %

### Predictor Performance
- **Pattern History Table (PHT) Hits:** TBD %
- **Return Address Stack (RAS) Hits:** TBD %

---

## 5. Out-of-Order Execution Metrics

### Reorder Buffer (ROB) Status
- **ROB Capacity:** 64 entries
- **Average ROB Occupancy:** TBD entries
- **ROB Full Events:** TBD
- **ROB Utilization:** TBD %

### Instruction Window Analysis
- **Instructions in Flight (avg):** TBD
- **Window Size Utilization:** TBD %
- **Dependency Chain Lengths:** TBD

### Register File Pressure
- **Physical Registers Used (avg):** TBD / 100
- **Register File Stalls:** TBD

### Write-back Port Saturation
- **Write-back Port Utilization:** TBD %
- **Contentions per 1000 cycles:** TBD

---

## 6. DOOM-Specific Workload Analysis

### Time Breakdown by Module
```
Game Logic:        TBD % (player input, physics, collision)
Rendering:         TBD % (screen buffer writes, sprite drawing)
Memory Operations: TBD % (level data, sprite cache)
Utility:           TBD % (math, sorting, utilities)
```

### Hotspot Functions
- **Top Function 1:** TBD (TBD % of time)
- **Top Function 2:** TBD (TBD % of time)
- **Top Function 3:** TBD (TBD % of time)

### Memory Footprint
- **Working Set (L1+L2):** TBD KB
- **Active Heap Usage:** TBD KB
- **Stack Depth:** TBD KB

---

## 7. System-Level Metrics

### Power Consumption (Estimated)
- **Average Power:** TBD mW
- **Peak Power:** TBD mW
- **Energy per Frame:** TBD mJ

### Thermal Profile
- **CPU Temperature (est):** TBD °C
- **Thermal Margin:** TBD °C

### Resource Utilization
- **LUT Usage:** TBD %
- **BRAM Usage:** TBD %
- **DSP Slices:** TBD %
- **Timing Slack:** TBD ns

---

## 8. Synthesis & Build Results

### Vivado Synthesis Metrics
- **Total Build Time:** TBD minutes
- **Synthesis Time:** TBD minutes
- **Place & Route Time:** TBD minutes
- **Bitstream Generation:** TBD minutes

### Timing Analysis
- **Worst Negative Slack (WNS):** TBD ns
- **Total Negative Slack (TNS):** TBD ns
- **Timing Met:** TBD (YES/NO)

### Area Utilization
- **LUTs:** TBD / 135,000 (TBD %)
- **BRAMs:** TBD / 312 (TBD %)
- **DSPs:** TBD / 600 (TBD %)

---

## 9. Comparison Targets

### Expected Performance (Day 3 Target)
| Metric | Target | Achieved | Status |
|--------|--------|----------|--------|
| FPS | 35+ | TBD | ⏳ |
| IPC | 2.5+ | TBD | ⏳ |
| L1-D Hit Rate | 80%+ | TBD | ⏳ |
| L2 Hit Rate | 70%+ | TBD | ⏳ |
| Branch Accuracy | 85%+ | TBD | ⏳ |
| ROB Utilization | 60%+ | TBD | ⏳ |

### Simulation vs. FPGA (Expected Gap)
- **RTL Simulation Speed:** ~1000x slower
- **Real FPGA Speed:** ~1x (native)
- **Measured Metrics:** Should be identical (cycle-accurate)

---

## 10. Analysis & Insights

### Bottleneck Analysis
**Primary Bottleneck:** TBD
- **Impact:** TBD % of performance loss
- **Root Cause:** TBD
- **Mitigation:** TBD

**Secondary Bottleneck:** TBD
- **Impact:** TBD % of performance loss

### Optimization Opportunities (Priority Order)
1. **TBD** - Estimated Impact: TBD % FPS gain
2. **TBD** - Estimated Impact: TBD % FPS gain
3. **TBD** - Estimated Impact: TBD % FPS gain

### Code Profile (Top 10 Hotspots)
```
Rank | Function           | % Time | Cycles | Calls
-----|-------------------|--------|--------|-------
  1  | TBD                | TBD%   | TBD    | TBD
  2  | TBD                | TBD%   | TBD    | TBD
  3  | TBD                | TBD%   | TBD    | TBD
  ...
```

---

## 11. Recommendations

### For Days 4-7 (FPGA Deployment)
- [ ] Implement TBD optimization (estimated +TBD% FPS)
- [ ] Tune L2 cache for better hit rate
- [ ] Optimize branch prediction accuracy
- [ ] Profile on real FPGA hardware

### For Days 8-14 (Kernel Optimization)
- [ ] Analyze kernel scheduling impact
- [ ] Profile memory contention
- [ ] Test with different CPU frequencies

### For Days 15-21 (Final Optimization)
- [ ] Fine-tune rendering pipeline
- [ ] Optimize memory access patterns
- [ ] Target 40+ FPS if possible

---

## 12. Raw Data Collection

### Simulator Console Output
```
[To be captured during test run]
```

### Performance Counter Values
```
[To be extracted from simulator statistics]
```

### Cycle-by-Cycle Trace (Sample)
```
Cycle | PC       | Instruction | Issue | ROB | L1-D-Hit | L2-Hit | Branch
------|----------|-------------|-------|-----|----------|--------|--------
  0   | 0x100e8  | addi sp,sp  | 1     | 1   | -        | -      | -
  1   | 0x100ec  | addi s0,sp  | 1     | 2   | -        | -      | -
  ... | ...      | ...         | ...   | ... | ...      | ...    | ...
```

---

## 13. Test Environment

### Hardware Configuration
- **CPU:** BOOM MediumV3Config
- **Pipeline Depth:** 8 stages
- **Issue Width:** 4 instructions/cycle
- **ROB Entries:** 64
- **Physical Registers:** 100 int + 64 FP

### Software Configuration
- **OS:** Linux 5.10.0-riscv64 (BuildRoot)
- **Compiler:** riscv64-unknown-elf-gcc (version TBD)
- **Optimization Flags:** -O2
- **Binary Size:** 100 KB

### Test Duration
- **Simulation Time:** TBD seconds
- **Simulated Cycles:** TBD
- **Real Time:** TBD minutes

---

## 14. Conclusion

**Summary:** TBD

**Key Finding:** TBD is the primary bottleneck, accounting for TBD% of performance loss.

**Recommendation:** Focus optimization efforts on TBD to achieve TBD% improvement.

**Next Steps:** TBD

---

**Test Conducted By:** Claude Code  
**Date:** 2026-08-18  
**Status:** ⏳ IN PROGRESS

