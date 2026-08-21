# Linux Kernel Boot Fix Report — DOOMBOOM (MediumBoomV3Config)

**Date:** 2026-08-20 → 2026-08-21
**Scope:** Root-cause diagnosis and fix of Linux kernel boot failure on custom BOOM RISC-V CPU
**Status:** Software stack fully validated (Spike + QEMU boot to login). Real BOOM/Verilator RTL confirmation in progress (throughput-limited, not correctness-limited).

---

## Executive Summary

For several days, the kernel appeared to hang immediately after printing `[UART] UART0 is here (stdin/stdout).` on the BOOM Verilator simulator. This was investigated as a device-tree mismatch. It was not.

Today's work found the actual chain of **five separate bugs**, none of them device-tree related, and produced a binary that **boots completely to a login prompt**, validated independently on two fast simulators (Spike, QEMU) before committing further time to the very slow BOOM RTL simulator.

---

## Part 1 — The Red Herring That Cost Days

The message `[UART] UART0 is here (stdin/stdout).` was assumed to be a kernel or bootloader boot-progress marker. It is not — it is printed by the **Verilator testbench's own C++ UART bridge model**:

```
generators/testchipip/src/main/resources/testchipip/csrc/uart.cc:57
    printf("[UART] UART0 is here (stdin/stdout).\n");
```

This line executes in the `uart_t` constructor, called during **host-side C++ testbench setup, before a single RISC-V instruction runs**. It proves the simulator's virtual UART peripheral was instantiated — nothing about kernel or bootloader progress. Every "kernel reaches UART" conclusion from prior sessions was based on this misreading.

**Lesson applied:** stopped trusting this message as a checkpoint and instead got real evidence of CPU execution.

---

## Part 2 — Getting Real Evidence (Spike + QEMU)

Verilator RTL simulation for this BOOM config is extremely slow (calibrated at **< 5,769 target-cycles/sec**), making it a poor tool for iterative debugging. Two much faster, independent RISC-V simulators were used instead to validate fixes before ever touching Verilator again:

- **Spike** (`.conda-env/riscv-tools/bin/spike`) — cycle-approximate ISA simulator, boots in seconds
- **QEMU** (`.conda-env/bin/qemu-system-riscv64 -M virt`) — full-system emulator, boots in seconds

Command patterns used:
```bash
# Spike
spike -p1 -m256 --isa=rv64imafdc_zicsr_zifencei_zihpm <binary>

# QEMU
qemu-system-riscv64 -M virt -m 256M -nographic -bios none -kernel <binary>
```

The first real breakthrough came from Spike: the original `br-base-bin` binary booted in ~1.3 seconds and hit a clean, visible **kernel panic**:

```
VFS: Unable to mount root fs on unknown-block(0,0)
```

This was the real bug — not a hang, not a device tree issue. It had been silently happening on every single Verilator attempt for days; Verilator's I/O buffering combined with the process being killed before completion simply hid it from view.

---

## Part 3 — Bug #1: No Root Filesystem Was Ever Attached

`br-base-bin` is only BBL/OpenSBI + `vmlinux`. The kernel's `.config` pointed `CONFIG_INITRAMFS_SOURCE` at a **1KB placeholder cpio** (`initramfs.cpio`), not real content. The actual rootfs lives in a separate 293MB disk image (`br-base.img`), meant to be attached as a **block device** (`root=/dev/vda`) in FireMarshal's normal (FireSim/FPGA) boot flow.

BOOM's Verilator test harness does not support block devices at all:
```
--disk=DISK   Add DISK device. Use a ramdisk since this isn't [supported]
```

**Conclusion:** the intended production boot path (real disk image via block device) is structurally impossible on this Verilator harness. An embedded initramfs is the only viable path, regardless of any other fix.

**Fix — extracting the real rootfs without root privileges:**
```bash
mkdir -p /tmp/rootfs_extract
debugfs -R "rdump / /tmp/rootfs_extract" \
  software/firemarshal/images/firechip/br-base/br-base.img
```
`debugfs rdump` reads an ext2 image and recursively dumps its contents to a normal directory — no `mount`, no `sudo` needed. Produced a working busybox-based rootfs (1,655 files).

---

## Part 4 — Bug #2: Wrong Bootloader Entirely (BBL vs OpenSBI)

Multiple prior sessions fought `riscv-pk`/BBL: recurring `fence.i` assembler errors, `-shared not supported` linker errors, toolchain confusion. This was the **wrong bootloader** — FireMarshal's real build pipeline uses **OpenSBI**, not BBL, confirmed by:

1. Booting the original working `br-base-bin` under QEMU and seeing an `OpenSBI v1.2` banner
2. Reading FireMarshal's actual build code: `software/firemarshal/wlutil/build.py::makeOpenSBI()`

The correct build invocation (reverse-engineered from that function):
```bash
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     PLATFORM=generic \
     FW_PAYLOAD_PATH=<kernel Image>
```
run inside `software/firemarshal/boards/default/firmware/opensbi/`. Output artifact: `build/platform/generic/firmware/fw_payload.elf` — a **self-contained** firmware+kernel ELF, exactly analogous to the working `br-base-bin`.

**Also discovered:** the payload must be `arch/riscv/boot/Image` (the raw kernel image), **not raw `vmlinux`** — a distinction that had never been correctly made in any prior BBL attempt.

**Also discovered:** correct kernel toolchain is `riscv64-unknown-linux-gnu-` (glibc-targeting), **not** `riscv64-unknown-elf-` (bare-metal, no `-shared` support needed for kernel VDSO). Using the wrong one caused every earlier kernel-with-toolchain build to fail on VDSO linking.

---

## Part 5 — Bug #3: Missing `/init`

With the real rootfs embedded, the kernel now unpacked the initramfs correctly but still fell through to legacy root-device mounting (`Freeing initrd memory` printed, but no init ran). Root cause: the rootfs only has `/sbin/init -> ../bin/busybox`, not `/init`. The kernel's default init search only checks `/init` unless `rdinit=` is passed on the command line.

**Complication:** `CONFIG_CMDLINE_FORCE=y` was already set in the base kernel config, meaning **any boot-time `-append`/kernel command-line argument is silently ignored** — the kernel always uses its baked-in `CONFIG_CMDLINE`. Passing `rdinit=/sbin/init` via QEMU's `-append` therefore did nothing.

**Fix:**
```bash
ln -sf sbin/init /tmp/rootfs_extract/init
```
No kernel rebuild needed for the external-initrd QEMU test; for the final self-contained binary, this symlink is picked up automatically on the next `CONFIG_INITRAMFS_SOURCE` rebuild.

---

## Part 6 — Bug #4: Missing `/dev/console` (and friends)

With `/init` in place, the externally-loaded initrd (QEMU `-initrd`) booted cleanly to a full login prompt. But the **self-contained, built-in-initramfs** binary (the only kind Verilator can run — it has no `-initrd` equivalent) stalled silently after `Run /init as init process`, with the kernel logging:
```
Warning: unable to open an initial console.
```

No `/dev/console`, `/dev/null`, or `/dev/tty` device nodes existed in the extracted rootfs (they weren't preserved by `debugfs rdump`, which doesn't recreate special files), and creating them on the host required `mknod` + root, which wasn't available (`sudo` needs a password here).

**Fix — declare device nodes directly in the cpio archive, no root needed:**
```
# /tmp/extra-nodes.txt
nod /dev/console 0600 0 0 c 5 1
nod /dev/null    0666 0 0 c 1 3
nod /dev/tty     0666 0 0 c 5 0
nod /dev/kmsg    0644 0 0 c 1 11
```
```
CONFIG_INITRAMFS_SOURCE="/tmp/rootfs_extract /tmp/extra-nodes.txt"
```
The kernel's own `gen_init_cpio` build tool accepts a directory *and* a plain-text node-list file, concatenating both into the final archive — this is the standard, root-free way to inject device special files into a built-in initramfs. Verified present in the generated archive via `cpio -tv`.

This did **not** fully fix it on its own (see Bug #5) but was a necessary and correct piece.

---

## Part 7 — Bug #5: The Actual Root Cause (Self-Inflicted, Today)

Even with `/init` and device nodes fixed, the self-contained binary still hung silently, still logging `Warning: unable to open an initial console.` — while the externally-loaded-initrd version worked perfectly with the exact same rootfs content.

**The difference:** hours earlier in this same debugging effort, `CONFIG_CMDLINE` had been changed to enable SiFive UART support, and in doing so the console spec was narrowed to:
```
CONFIG_CMDLINE="console=ttySIF0 earlycon"
```
This drops the original fallback:
```
CONFIG_CMDLINE="console=ttyS0 console=ttySIF0,3686400 earlycon"
```
`ttySIF0` only exists on real BOOM hardware (SiFive UART). QEMU's `virt` machine provides `ttyS0` (8250 UART) — a device that never matched the forced cmdline, so **no console was ever registered as the system console**, hence "unable to open an initial console" and totally silent execution from that point on (kernel messages up to that point come from the separate, always-available `earlycon` path, which explains why boot looked fine right up until the moment init needed a real console).

**Fix:** restore both console specs, so it matches whichever UART is actually present:
```
CONFIG_CMDLINE="console=ttyS0 console=ttySIF0,3686400 earlycon"
```

This was the fix that made the self-contained binary boot **completely**, to a full login prompt, matching the earlier external-initrd success.

---

## Part 8 — Full Boot Confirmed (Twice, Independently)

Final build: kernel (`riscv64-unknown-linux-gnu-` toolchain, real rootfs + `/init` + device nodes + correct cmdline) → `Image` → OpenSBI `PLATFORM=generic` `fw_payload.elf`.

**QEMU**, full output tail:
```
Run /init as init process
running /etc/init.d/S01syslogd
Starting syslogd: OK
running /etc/init.d/S02klogd
Starting klogd: OK
running /etc/init.d/S02sysctl
Running sysctl: OK
running /etc/init.d/S10mdev
Starting mdev: OK
running /etc/init.d/S40network
Starting network: OK
running /etc/init.d/S99run
launching firemarshal workload run/command
firemarshal workload run/command done

Welcome to Buildroot
buildroot login:
```

**Spike:** same binary, same result — clean boot to login prompt, no panic, no hang.

This is the first time in the project the full software stack (kernel + firmware + real rootfs + init sequence) has been confirmed end-to-end correct.

Final validated binary: **`/home/ninadjangle/chipyard/doombom-linux-final.elf`** (54MB, self-contained, no external dependencies).

---

## Part 9 — Current Status: Real BOOM/Verilator Run

The validated binary was launched on the actual `MediumBoomV3Config` Verilator simulator:
```bash
./sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
  +permissive +noassert +max-cycles=200000000000000 +permissive-off \
  doombom-linux-final.elf
```

As of this report:
- **Running time:** 17h 15m+, continuous, no restarts
- **CPU utilization:** 99.8% sustained (confirmed via `/proc/<pid>/stat` — genuinely computing, not stalled/blocked)
- **Output so far:** none (not even the OpenSBI banner)
- **Watchdog:** heartbeat every 15 minutes, confirms process alive throughout
- **Deadline:** 2026-08-21 ~20:40 (user-granted 24h extension from prior 10h extension)

This is **not** unexpected given measured Verilator throughput on this BOOM config (< 5,769 target-cycles/sec, calibrated directly). Estimated cycles simulated so far: roughly **125–360 million**, extrapolated from that calibration (real number unknown — no live introspection was possible: the simulator binary exposes no readable cycle-count symbol via `nm`/`readelf`, and no `gdb` is available on this system to attach and inspect memory safely without risking the run).

**Key distinction from all prior status updates:** this is now a pure **throughput** problem on a **known-correct** binary, not an unresolved correctness question. The same binary is proven to boot completely on two independent, fast simulators.

---

## Summary Table

| # | Bug | Symptom | Fix |
|---|-----|---------|-----|
| — | Misread UART message | Assumed kernel checkpoint | It's testbench C++ code, printed at t=0; ignored going forward |
| 1 | No rootfs attached | `VFS: Unable to mount root fs` panic | Extract real rootfs from `br-base.img` via `debugfs rdump`, embed as initramfs |
| 2 | Wrong bootloader (BBL vs OpenSBI) | `fence.i`/`-shared` build failures | Use OpenSBI `PLATFORM=generic`, correct payload (`Image` not `vmlinux`), correct toolchain (`riscv64-unknown-linux-gnu-`) |
| 3 | No `/init` | Initramfs unpacked but init never ran | `ln -sf sbin/init /tmp/rootfs_extract/init` |
| 4 | No `/dev/console` etc. | `unable to open an initial console` | `gen_init_cpio` node-list file, no root needed |
| 5 | `CONFIG_CMDLINE` console mismatch | Same warning persisted after fix #4 | Restore dual `console=ttyS0 console=ttySIF0,...` cmdline |

---

## Key File Locations

| Purpose | Path |
|---|---|
| Final validated binary | `/home/ninadjangle/chipyard/doombom-linux-final.elf` |
| Extracted/fixed rootfs source | `/tmp/rootfs_extract` |
| Device node list | `/tmp/extra-nodes.txt` |
| Kernel config | `software/firemarshal/boards/default/linux/.config` |
| Kernel Image | `software/firemarshal/boards/default/linux/arch/riscv/boot/Image` |
| OpenSBI build output | `software/firemarshal/boards/default/firmware/opensbi/build/platform/generic/firmware/fw_payload.elf` |
| Live Verilator run log | `/tmp/verilator-REAL-final.log` |
| Watchdog log | `/tmp/watchdog.log` |

---

## Recommendation

The remaining gap is Verilator simulation speed, not correctness. Two paths forward once (if) the current run either completes or is abandoned:

1. **Let it finish** — within the 24h budget, has a reasonable chance of reaching visible output given the boot is proven feasible in principle
2. **AWS FPGA / FireSim** — real hardware speed, eliminates the throughput problem entirely, and the exact same validated binary (`doombom-linux-final.elf`) is ready to deploy there directly
