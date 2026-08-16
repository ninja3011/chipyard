# DOOM3 Porting Plan - RISC-V RV64 Target

**Status**: Phase 3 Preparation (Pre-FPGA deployment)  
**Target**: Linux on BOOM CPU (Verilator sim + AWS FPGA)  
**Goal**: Interactive DOOM3 gameplay at 30+ FPS

---

## Strategy

Instead of porting the full DOOM3 engine (which is complex), we'll use a **modular approach**:

1. **Option A (Recommended)**: Use **doomgeneric** - minimal DOOM1 port
   - Smaller codebase (~5K LOC)
   - Framebuffer rendering (no GPU needed)
   - Runs on bare-metal/embedded systems
   - Easily cross-compilable

2. **Option B**: DOOM3 BFG Edition (if source available)
   - Full DOOM3 graphics
   - More demanding
   - Complex dependencies

3. **Option C**: Custom minimal renderer
   - Triangle rasterizer in C
   - 100-200 LOC
   - Pure software rendering

---

## Phase 3 Timeline (Days 15-21)

| Day | Task | Time |
|-----|------|------|
| 15-16 | Get source + cross-compile | 2 hours |
| 17-18 | Custom graphics backend | 2 hours |
| 19 | Simulator testing + profiling | 2 hours |
| 20 | Performance optimization | 2 hours |
| 21 | FPGA deployment + final demo | 2 hours |

**Buffer**: 9 days remaining for troubleshooting/optimization

---

## Approach: doomgeneric

### Why doomgeneric?

✅ **Pros**:
- GPL license (free, open source)
- Minimal dependencies
- Runs on embedded systems
- ~5K lines of C code
- Pure software rendering
- Easy to port to any platform with framebuffer

❌ **Cons**:
- DOOM1, not DOOM3
- Lower graphics quality
- But: DOOM1 is fun and iconic!

### Architecture

```
┌──────────────────────────────────────┐
│      doomgeneric main game logic     │
│  (game simulation, AI, physics)      │
└────────────────┬─────────────────────┘
                 │
    ┌────────────▼─────────────────┐
    │  Custom Graphics Backend     │
    │  ├─ Framebuffer writer       │
    │  ├─ Palette rendering        │
    │  └─ UART console output      │
    └────────────┬────────────────┘
                 │
    ┌────────────▼─────────────────┐
    │    Linux kernel framebuffer  │
    │    /dev/fb0 or /dev/mem      │
    └──────────────────────────────┘
```

---

## Implementation Steps

### Step 1: Get Source (15 min)

**doomgeneric** is open source:
```bash
git clone https://github.com/ozkl/doomgeneric.git
cd doomgeneric
```

Includes:
- DOOM1 executable (shareware)
- doomgeneric.c (main loop)
- platform-specific backends (examples)

### Step 2: Build Environment Setup (30 min)

```bash
# Activate RISC-V toolchain
source /home/ninadjangle/chipyard/.conda-env/etc/profile.d/conda.sh
conda activate /home/ninadjangle/chipyard/.conda-env

# Verify toolchain
riscv64-unknown-elf-gcc --version
riscv64-unknown-elf-objdump --version
```

### Step 3: Cross-Compile doomgeneric (1 hour)

Create custom `platform.c` for RV64:

```c
// platform.c - RISC-V framebuffer backend
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "doomgeneric.h"

#define FB_WIDTH 320
#define FB_HEIGHT 200
#define FB_DEPTH 8  // 8-bit color (256 colors)
#define FB_SIZE (FB_WIDTH * FB_HEIGHT * (FB_DEPTH/8))

static int fb_fd = -1;
static uint8_t *fb_mem = NULL;

// Initialize framebuffer
void DG_Init() {
    // Open framebuffer device
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("Failed to open /dev/fb0");
        exit(1);
    }
    
    // Map framebuffer to userspace
    fb_mem = mmap(NULL, FB_SIZE, PROT_READ | PROT_WRITE, 
                  MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("Failed to mmap framebuffer");
        exit(1);
    }
    
    printf("[DOOM3-PORT] Framebuffer initialized: %dx%d @ 8-bit\n", 
           FB_WIDTH, FB_HEIGHT);
}

// Render frame to framebuffer
void DG_DrawFrame() {
    // Copy game screen (320x200x8) to framebuffer
    memcpy(fb_mem, DG_ScreenBuffer, FB_SIZE);
}

// Input handling (keyboard/mouse)
void DG_SleepMs(uint32_t ms) {
    usleep(ms * 1000);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    // TODO: Read from /dev/input or UART
    return 0;
}

// Cleanup
void DG_Cleanup() {
    if (fb_mem) munmap(fb_mem, FB_SIZE);
    if (fb_fd >= 0) close(fb_fd);
}
```

**Compilation**:
```bash
riscv64-unknown-elf-gcc \
  -O2 \
  -march=rv64imafd \
  -mabi=lp64d \
  -nostdlib \
  -static \
  -o doom3.riscv \
  doomgeneric.c platform.c \
  -lm  # math library
```

### Step 4: Create Custom Graphics Backend (1 hour)

Minimal rendering pipeline:
- Input: DOOM game screen (320×200 pixels, 8-bit indexed color)
- Output: Framebuffer write to `/dev/fb0`
- Color palette: Standard DOOM palette (256 colors)

**Key optimizations for RISC-V**:
- Use 64-bit memcpy (matches CPU word size)
- Cache-friendly memory layout
- Minimal branching (straight memory copy)

### Step 5: Test on Simulator (2 hours)

```bash
# 1. Boot Linux on BOOM simulator
/home/ninadjangle/chipyard/sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
  /home/ninadjangle/chipyard/software/firemarshal/images/firechip/br-base/br-base-bin

# 2. Inside Linux, run DOOM:
./doom3.riscv

# 3. Monitor:
# - Framebuffer updates (via /dev/fb0)
# - Performance metrics (cycles, cache misses)
# - System resources
```

### Step 6: Profile & Optimize (2 hours)

**Metrics to measure**:
- Frame rate (target: 30+ FPS)
- CPU utilization
- Memory bandwidth
- Cache hit/miss ratio
- Rendering time per frame

**Optimizations**:
- SIMD parallelization (RVV vector extension)
- Cache prefetching
- Memory layout optimization
- Assembly-optimized memcpy

---

## Expected Performance

### Baseline (unoptimized)
- Frame time: ~50-100ms (10-20 FPS)
- CPU utilization: 40-60%
- Memory bandwidth: 30-50% utilization

### Optimized (with tuning)
- Frame time: ~30-35ms (28-33 FPS) ✅
- CPU utilization: 80-90%
- Memory bandwidth: 70-80% utilization

---

## Fallback Plans

| Issue | Fallback |
|-------|----------|
| DOOM3 too slow | Reduce resolution (160×100) or use DOOM1 |
| Framebuffer unavailable | Write to UART console (ASCII art) |
| Rendering crashes | Debug with printf logging |
| Compilation fails | Pre-built binary from Raspberry Pi repo |

---

## Files to Create

```
doom3-port/
├── DOOM3-PORTING-PLAN.md (this file)
├── doomgeneric/          (DOOM1 source)
├── platform.c            (RV64 framebuffer backend)
├── platform.h            (header file)
├── Makefile              (cross-compilation rules)
├── doom3.riscv           (compiled binary)
└── profiler.c            (performance measurement)
```

---

## Success Criteria

✅ **Phase 3 Success** (Days 15-21):
1. [ ] doomgeneric source obtained
2. [ ] Cross-compiled for RV64
3. [ ] Custom framebuffer backend working
4. [ ] DOOM runs on simulator with Linux
5. [ ] 30+ FPS achieved
6. [ ] Deployed to AWS FPGA
7. [ ] Interactive gameplay working
8. [ ] Demo video captured

---

## References

- **doomgeneric**: https://github.com/ozkl/doomgeneric
- **DOOM1 source**: https://github.com/id-Software/DOOM
- **RISC-V ABI**: https://github.com/riscv/riscv-elf-psabi-doc
- **Linux framebuffer**: https://www.kernel.org/doc/html/latest/fb/

---

## Next Actions

1. **Immediately** (Today):
   - Clone doomgeneric repo
   - Set up build environment
   - Start cross-compilation

2. **Tomorrow** (Day 15):
   - Platform backend implementation
   - Test on simulator

3. **Day 16-17**:
   - Performance profiling
   - Optimization passes

4. **Day 18-19**:
   - Final integration
   - AWS FPGA deployment

5. **Day 20-21**:
   - Interactive testing
   - Demo video

---

**Status**: Ready to begin Phase 3 porting  
**Time to interactive DOOM**: ~6-8 hours of work + 4-6 hours FPGA synthesis  
**Confidence**: High (proven approach, well-documented codebase)

