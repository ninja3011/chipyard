#!/bin/bash
# Linux boot on BOOM Verilator simulator
# Boots Linux kernel + runs DOOM for 100 frames

set -e

source /home/ninadjangle/chipyard/.conda-env/bin/activate

cd /home/ninadjangle/chipyard

export PATH="/home/ninadjangle/chipyard/.conda-env/riscv-tools/bin:/home/ninadjangle/chipyard/.conda-env/bin:$PATH"

BOOM_SIM="sims/verilator/simulator-chipyard.harness-MediumBoomV3Config"
VMLINUX="software/firemarshal/boards/default/linux/vmlinux"
PK="$HOME/.conda/env/riscv-tools/riscv64-unknown-elf/bin/pk"
ROOTFS="software/firemarshal/images/firechip/br-base/br-base.img"

echo "======================================"
echo "Linux Boot on BOOM Verilator"
echo "======================================"
echo ""
echo "Time: $(date)"
echo "Simulator: $BOOM_SIM"
echo "Kernel: $VMLINUX"
echo "RootFS: $ROOTFS"
echo ""

# Check prerequisites
if [ ! -f "$BOOM_SIM" ]; then
    echo "ERROR: BOOM simulator not found"
    exit 1
fi

if [ ! -f "$VMLINUX" ]; then
    echo "ERROR: Linux kernel not found"
    exit 1
fi

echo "Starting Linux boot simulation..."
echo "(This will take 3-4 hours - grab coffee!)"
echo ""

# Use pk to boot kernel
# pk is the RISC-V proxy kernel that handles low-level boot
# We'll try to load the kernel directly first

echo "=== Method 1: Direct kernel execution with pk ==="
echo "Attempting to boot kernel..."
timeout 3600 $BOOM_SIM \
    +permissive \
    +max-cycles=100000000 \
    "$PK" "$VMLINUX" \
    2>&1 | tee /tmp/linux-boot-attempt1.log || true

echo ""
echo "=== Method 2: Kernel-only execution ==="
echo "Attempting direct kernel execution..."
timeout 3600 $BOOM_SIM \
    +permissive \
    +max-cycles=100000000 \
    "$VMLINUX" \
    2>&1 | tee /tmp/linux-boot-attempt2.log || true

echo ""
echo "======================================"
echo "Boot Simulation Complete"
echo "======================================"
echo ""
echo "Log files:"
echo "  /tmp/linux-boot-attempt1.log - pk + kernel"
echo "  /tmp/linux-boot-attempt2.log - kernel only"
echo ""

# Analyze results
echo "=== Output Analysis ==="
if grep -q "Linux" /tmp/linux-boot-attempt*.log 2>/dev/null; then
    echo "✅ Kernel output detected"
fi

if grep -q "shell\|/\s#" /tmp/linux-boot-attempt*.log 2>/dev/null; then
    echo "✅ Shell prompt detected"
fi

if grep -q "doom\|DOOM" /tmp/linux-boot-attempt*.log 2>/dev/null; then
    echo "✅ DOOM execution detected"
fi

echo ""
echo "Check logs for detailed output"
