# Linux Boot on BOOM Verilator Simulator

**Objective:** Boot Linux kernel + run DOOM on Verilator BOOM simulator  
**Timeline:** 6-7 hours wall time  
**Status:** Planning & preparation phase

---

## Available Resources

### Pre-built Components
- **vmlinux kernel:** `/home/ninadjangle/chipyard/software/firemarshal/boards/default/linux/vmlinux`
  - Size: 22MB
  - Architecture: RV64
  - Entry point: 0xffffffff80000000 (kernel virtual, Sv39 paging)

- **Buildroot rootfs:** `/home/ninadjangle/chipyard/software/firemarshal/images/firechip/br-base/br-base.img`
  - Size: 293MB ext2 filesystem
  - Contains: busybox, shell, standard utilities

- **Device tree:** `/home/ninadjangle/chipyard/software/firemarshal/boards/default/linux/drivers/of/empty_root.dtb`
  - Hardware description for kernel

- **Toolchain:** `riscv64-unknown-linux-gnu-*` (available in conda env)

---

## Linux Boot Strategy for Verilator

### Challenge: Memory Constraints
Verilator simulator has virtual memory limits:
- 293MB rootfs is too large for typical simulator memory
- Solution: Use HTIF (Host-Target Interface) protocol to stream filesystem

### Boot Flow
```
1. Bootloader (runs first)
   ↓
2. Loads kernel via HTIF
   ↓
3. Kernel initializes memory management (Sv39 paging)
   ↓
4. Kernel mounts rootfs from HTIF device
   ↓
5. Init process starts shell
   ↓
6. Load DOOM binary from rootfs
   ↓
7. Execute DOOM for 100 frames
   ↓
8. Shutdown
```

---

## Implementation Plan

### Phase 1: Enhanced Bootloader (30 min)
Create bootloader that:
1. Initializes exception handlers
2. Sets up UART for kernel console
3. Sets up HTIF for block device (rootfs)
4. Jumps to kernel entry point (0xffffffff80000000)
5. Passes device tree pointer and hart ID

**File:** `doom3-port/linux-boot.s` (enhanced from qemu-boot.s)

### Phase 2: Kernel Configuration Check (15 min)
Verify kernel has:
- [ ] Sv39 paging support (for virtual addressing)
- [ ] HTIF block device driver (for rootfs)
- [ ] Console support via UART
- [ ] Initramfs or rootfs mount support
- [ ] Init process (busybox init)

**Command:** `riscv64-unknown-linux-gnu-objdump -t vmlinux | grep init`

### Phase 3: DOOM Binary Integration (30 min)
1. Extract rootfs from br-base.img
2. Add doom.riscv binary to /usr/bin/
3. Add startup script to /etc/init.d/
4. Repackage rootfs or use overlay

**Script:** Copy doom-qemu-test-spike.riscv into rootfs

### Phase 4: Bootloader Assembly (1 hour)
Assemble and link enhanced bootloader with kernel + device tree + rootfs image

**Linker script:** Modified to include kernel binary at 0x80000000

### Phase 5: Verilator Simulation (3-4 hours)
Run: `simulator-chipyard.harness-MediumBoomV3Config +max-cycles=10000000 linux-boot.elf`

**Expected output:**
```
[UART] UART0 is here
[kernel] Linux version ...
[kernel] Starting init
[init] Starting shell
[shell] / # 
[shell] # /usr/bin/doom-qemu-test-spike.riscv
[doom] Game logic: 100 frames executed
[doom] Exit: SUCCESS
```

**Simulation time:** ~4 hours for full boot + execution

---

## Timeline Estimate

| Phase | Task | Time | Status |
|-------|------|------|--------|
| 1 | Bootloader enhancement | 30 min | TODO |
| 2 | Kernel config check | 15 min | TODO |
| 3 | DOOM binary integration | 30 min | TODO |
| 4 | Bootloader assembly | 1 hour | TODO |
| 5 | Verilator simulation | 3-4 hours | TODO |
| 6 | Analysis & documentation | 30 min | TODO |
| **Total** | | **6-7 hours** | **TODO** |

---

## Success Criteria

### Minimum (Linux boots)
- ✅ UART output shows kernel messages
- ✅ Shell prompt appears
- ✅ Shell accepts commands
- ✅ Can list files (`ls /`)

### Full (DOOM runs on Linux)
- ✅ DOOM binary executes from shell
- ✅ Game logic runs for 100 frames
- ✅ Exit code 0 (success)
- ✅ Total simulation completes

### Validation
- Compare boot sequence with FPGA boot (later)
- Verify kernel messages are identical
- Confirm DOOM runs identically on both platforms

---

## Technical Details

### Kernel Virtual Address Space
The kernel runs in high virtual addresses (Sv39 paging):
- Kernel VA: 0xffffffff80000000 - 0xffffffffffffffff
- Maps to PA: 0x00000000 - 0x7fffffff (physical)
- Bootloader must set up page tables before jumping to kernel

### HTIF Protocol
HTIF allows host (Verilator) to provide:
- Console I/O (UART)
- Block device (rootfs)
- System calls (exit)

Kernel driver: `drivers/char/riscv_htif.c`

### Rootfs Mounting
Kernel will attempt to mount rootfs from HTIF device:
- Device: `/dev/htif0` (block device)
- Mount point: `/` (root)
- Filesystem: ext2

---

## Potential Issues & Mitigations

| Issue | Probability | Mitigation |
|-------|-------------|-----------|
| Kernel doesn't boot (hangs) | Medium | Check bootloader, page tables, UART |
| Rootfs mount fails | High | Verify HTIF driver, kernel config |
| DOOM binary not in rootfs | High | Extract img, add binary, repack |
| Simulation runs out of memory | Medium | Use HTIF streaming instead of loading all |
| Slow simulation (4+ hours) | Very High | Expected - just wait |

---

## Fallback Plan

If full Linux boot fails:
1. Boot to kernel panic message (still validates kernel loads)
2. Check kernel log for specific error
3. Adjust bootloader or kernel config
4. Retry specific phase

If DOOM fails on Linux:
1. DOOM still runs on bare metal (proven)
2. At least kernel boot is validated
3. Can proceed to FPGA with knowledge of boot sequence

---

## Next Steps

1. ✅ **Verify vmlinux and rootfs exist** (DONE)
2. **Create enhanced bootloader** (START HERE)
3. **Integrate DOOM into rootfs**
4. **Run Verilator simulation**
5. **Validate kernel boot + DOOM execution**
6. **Document results for FPGA deployment**

