# Day 4: Performance Verification & Benchmarking Plan

**Date:** August 18, 2026 (Day 4)  
**Objective:** Collect actual performance metrics from DOOM binary running on BOOM simulator  
**Timeline:** Full day (simulator test in progress, measurements pending)

---

## Day 4 Goals

- [ ] **Collect actual performance data** from simulator execution
- [ ] **Benchmark DOOM binary** on BOOM MediumV3Config architecture
- [ ] **Verify estimates vs actual** performance (Day 3 estimates)
- [ ] **Identify bottlenecks** from real execution
- [ ] **Quantify performance gaps** (if any)
- [ ] **Document findings** in comprehensive report
- [ ] **Plan optimizations** for Day 5 (if needed)

---

## Current Status

**Simulator Test:** RUNNING
- Start Time: 2026-08-18 23:07:06
- Elapsed: 7+ minutes
- Status: Linux kernel initialization (slow in RTL)
- Expected Duration: 10-15 minutes total
- Output Log: `test-run.log` (waiting for data)

**What's Happening:**
1. Verilator compiling RTL design (background)
2. Linux kernel booting in simulated memory
3. DOOM binary queued for execution
4. Metrics collection framework ready

---

## Metrics Collection Strategy

### Phase 1: Capture Simulator Output (Automatic)
When simulator completes, log will contain:
```
[BOOT] Linux starting...
[DOOM] Platform initialized
[DOOM] Test binary: doom.riscv
[DOOM] Starting game loop...
[FRAME 100] Running...
[FRAME 200] Running...
...
[SUCCESS] Test complete! Frames: 1000
```

### Phase 2: Parse Raw Output (Manual)
Extract from logs:
- Total frames rendered
- Total simulation time (if available)
- Any performance counters
- Error messages or anomalies

### Phase 3: Calculate Derived Metrics
```
Measured IPC = Total Instructions / Total Cycles
Measured FPS = Frames / (Simulated Seconds)
Measured Frame Time = Total Cycles / Frames
```

### Phase 4: Compare to Estimates
```
Estimated FPS: 31.5
Actual FPS: [from measurement]
Delta: [percentage difference]
```

---

## Expected Benchmarking Data

### Primary Metrics to Collect

**1. Execution Summary**
```
Test Duration (simulated): _____ seconds
Total Frames Rendered: _____
Total CPU Cycles: _____
Total Instructions: _____
Test Status: _____ (Success/Partial/Failed)
```

**2. Performance Metrics**
```
FPS (Frames Per Simulated Second): _____
Cycles Per Frame: _____
IPC (Instructions Per Cycle): _____
Frame Time (ms): _____
Frame Time Variance: _____
```

**3. Cycle Breakdown (if available from trace)**
```
Total Cycles:        _____ (100%)
  Productive:        _____ (X%)
  Stalled (L1 miss): _____ (X%)
  Stalled (L2 miss): _____ (X%)
  Stalled (Branch):  _____ (X%)
  Other stalls:      _____ (X%)
```

**4. Memory Subsystem (if available from stats)**
```
L1-I Cache Hit Rate: _____ %
L1-D Cache Hit Rate: _____ %
L2 Cache Hit Rate: _____ %
DRAM Accesses: _____
Memory Bandwidth Used: _____ GB/s
```

**5. Branch Prediction (if available)**
```
Total Branches: _____
Correct Predictions: _____ %
Mispredictions: _____ %
Misprediction Penalty: _____ cycles average
```

**6. Out-of-Order Execution (if available)**
```
Average ROB Occupancy: _____
Peak ROB Occupancy: _____
Instructions in Flight (avg): _____
Register File Stalls: _____
```

---

## Estimation vs Reality Comparison Template

| Metric | Estimated | Measured | Delta | Status |
|--------|-----------|----------|-------|--------|
| FPS | 31.5 | ___ | ±_% | ⏳ |
| IPC | 2.4 | ___ | ±_% | ⏳ |
| L1-D Hit Rate | 86% | ___% | ±_% | ⏳ |
| L2 Hit Rate | 78% | ___% | ±_% | ⏳ |
| Branch Accuracy | 89% | ___% | ±_% | ⏳ |
| ROB Utilization | 59% | ___% | ±_% | ⏳ |
| Cycles/Frame | 28,800 | ___ | ±_% | ⏳ |

---

## Analysis Methodology

### If Actual Matches Estimates (+/- 10%)
- ✅ Validates BOOM architecture models
- ✅ Confirms simulation accuracy
- ✅ Proceed with Phase 2 (FPGA deployment) as planned
- ✅ Use estimates for optimization roadmap

### If Actual EXCEEDS Estimates (Better Performance)
- ✅ Surprise upside!
- 📊 Analyze what's causing better performance
- 🎯 More headroom for optimization (Days 8-21)
- 📈 Potential to exceed 35 FPS target

### If Actual UNDERPERFORMS Estimates (Worse)
- ⚠️ Investigate root cause
- 🔍 Identify unexpected bottleneck
- 🛠️ Plan mitigation for Day 5 optimization
- ⏰ May need to adjust timeline if significant gap

---

## Bottleneck Analysis Framework

**If Performance Gap Exists:**

### Step 1: Identify Primary Bottleneck
```
Is IPC lower than expected?
  YES → Pipeline stall issue
         → Check: cache misses, branch mispredictions, dependencies
  NO → Something else limits performance
```

### Step 2: Locate Bottleneck
```
Cache Miss Heavy? (L1/L2 hit rates low)
  → Memory layout issue in DOOM code
  → Optimization: Better cache locality, prefetching

Branch Misprediction Heavy? (>11% misprediction rate)
  → Complex branching in physics/collision code
  → Optimization: Loop unrolling, branch prediction hints

Dependency Heavy? (many RAW hazards)
  → Algorithm limited by data dependencies
  → Optimization: Out-of-order execution limits reached
```

### Step 3: Estimate Impact
```
If bottleneck is 5% of cycles → Max gain if fixed: 5%
If bottleneck is 20% of cycles → Max gain if fixed: 20%
```

### Step 4: Plan Mitigation
```
Priority = Impact × Feasibility × Time
```

---

## Data Collection Tools

### Available from Simulator
1. **UART Console Output** ✓ (primary, text-based)
2. **VCD Waveform Trace** (optional, detailed)
3. **Performance Counters** (if enabled in RTL)

### Collection Method
```bash
# Primary: Parse console output
grep "\[DOOM\]" test-run.log
grep "\[FRAME" test-run.log
grep "SUCCESS\|COMPLETE\|FAILED" test-run.log

# Secondary: Extract timing info
grep "time\|cycle\|FPS" test-run.log
```

### Analysis Tools
- Text parsing (grep, awk)
- Spreadsheet analysis (LibreOffice, Excel)
- Python scripts for statistical analysis (if needed)

---

## Expected Outcomes (Day 4)

### Scenario A: Measurements Match Estimates ✅
**Action:** Document findings, confirm BOOM architecture models are accurate
```
Actual FPS: 31.5 (±10%)
Actual IPC: 2.4 (±10%)
→ Proceed with Phase 2 FPGA deployment
→ Use Day 5 for optimizations (not critical fixes)
```

### Scenario B: Performance Better Than Expected 🎉
**Action:** Celebrate, analyze why, plan aggressive Day 5 optimizations
```
Actual FPS: 35+ (exceeds target)
Actual IPC: 2.6+
→ Proceed with Phase 2 FPGA deployment
→ Use Day 5 for enhancements (higher refresh rates, graphics)
→ Target 40+ FPS on FPGA
```

### Scenario C: Performance Lower Than Expected ⚠️
**Action:** Investigate gap, plan targeted optimizations
```
Actual FPS: 25 (gap: -20%)
Actual IPC: 2.0
→ Investigate bottleneck (memory? branches? dependencies?)
→ Use Day 5 for critical optimization (must close gap)
→ Adjust timeline if needed
```

---

## Day 4 Schedule

| Time | Task | Status |
|------|------|--------|
| Now | Simulator running (Linux boot) | ⏳ ONGOING |
| +5-10 min | Simulator produces output | ⏳ WAITING |
| +15-20 min | Parse and extract metrics | ⏳ PENDING |
| +30 min | Calculate derived metrics | ⏳ PENDING |
| +45 min | Compare to estimates | ⏳ PENDING |
| +60 min | Identify bottlenecks (if any) | ⏳ PENDING |
| +90 min | Document findings | ⏳ PENDING |
| +120 min | Create Day 5 optimization plan | ⏳ PENDING |
| End of Day | Commit results to git | ⏳ PENDING |

---

## Success Criteria (Day 4)

- [ ] Simulator test completes successfully
- [ ] Raw performance data extracted from logs
- [ ] All metrics calculated (IPC, FPS, cache hit rates, etc.)
- [ ] Comparison: Estimated vs Actual completed
- [ ] Bottleneck analysis (if gaps exist) documented
- [ ] Day 5 optimization plan (if needed) defined
- [ ] Results committed to git
- [ ] PERFORMANCE-METRICS.md updated with actual data

---

## Key Questions to Answer (Day 4)

1. **Did the binary execute correctly?**
   - Yes/No: ___
   - Evidence: ___

2. **Did we achieve the target 31.5 FPS (in simulated time)?**
   - Estimated: 31.5 FPS
   - Measured: ___ FPS
   - Gap: ___% (acceptable if within ±10%)

3. **What was the bottleneck?**
   - Primary: ___
   - Impact: ___%
   - Mitigation needed? Yes/No

4. **Is BOOM architecture performing as modeled?**
   - IPC: Estimated 2.4 vs Measured ___
   - Cache: Estimated 86%/78% vs Measured ___/___
   - Branch: Estimated 89% vs Measured ___

5. **Are we ready for Phase 2 (FPGA deployment)?**
   - Yes → Proceed with AWS
   - No → Define blockers and mitigation

---

## Notes & Observations

### Simulator Behavior (Day 4 Morning)
- Started: 2026-08-18 23:07:06
- Linux boot time: Slower than expected (RTL cycle-accurate)
- Output capture: Pending
- Observations: _______________

### Data Quality Notes
- RTL simulation is cycle-accurate (metrics valid)
- Real-time execution is 1000x slower (but measurements remain valid)
- All percentage calculations based on cycle counts
- No approximations or estimates in final metrics

---

## Deliverables (Day 4)

### Required
- [ ] `test-run.log` - Complete simulator output
- [ ] `PERFORMANCE-METRICS.md` - Updated with actual data
- [ ] Bottleneck analysis (if applicable)
- [ ] Day 5 optimization plan (if needed)

### Optional
- [ ] Performance comparison charts
- [ ] Cycle-level trace analysis (if VCD available)
- [ ] Statistical analysis of frame time variance

---

## Transition to Day 5

**If measurements successful:**
```
Day 4 → Day 5 Handoff:
  ✓ Actual metrics captured
  ✓ Bottlenecks identified (if any)
  ✓ Optimization targets defined
  ✓ Ready for performance tuning
```

**Day 5 will focus on:**
- Compiler flags optimization
- Memory layout tuning
- Branch prediction improvement
- Platform-specific optimizations

---

## Notes

- Simulator still running (7+ min elapsed, expected 10-15 min total)
- Once test completes, data extraction will be fast (<5 min)
- Will update this document with actual measurements
- Timeline remains on schedule even with slow RTL simulation

---

**Status:** WAITING FOR SIMULATOR OUTPUT  
**Next Update:** When test-run.log contains performance data  
**Owner:** Claude Code + Benchmarking Framework

