#!/bin/bash

# FIX: Apply BOOM-specific device tree to Linux kernel
# Root cause: Kernel expecting devices not in BOOM test harness

set -e

cd /home/ninadjangle/chipyard

echo "=========================================="
echo "FIX: Applying BOOM Device Tree"
echo "=========================================="

# Use the generated device tree from test harness
BOOM_DTS="./sims/verilator/generated-src/chipyard.harness.TestHarness.MediumBoomV3Config/chipyard.harness.TestHarness.MediumBoomV3Config.dts"
KERNEL_PATH="./software/firemarshal/boards/default/linux"

if [ ! -f "$BOOM_DTS" ]; then
  echo "✗ BOOM device tree not found at $BOOM_DTS"
  exit 1
fi

echo "Step 1: Copy BOOM device tree to kernel build dir"
cp "$BOOM_DTS" "$KERNEL_PATH/arch/riscv/boot/dts/boom.dts"

echo "Step 2: Update kernel config to use BOOM device tree"
cd "$KERNEL_PATH"

# Add BOOM DTS to device tree list
if ! grep -q "CONFIG_BUILTIN_DTB_SOURCE_PATH" .config; then
  echo 'CONFIG_BUILTIN_DTB_SOURCE_PATH="arch/riscv/boot/dts/boom.dts"' >> .config
else
  sed -i 's|CONFIG_BUILTIN_DTB_SOURCE_PATH=.*|CONFIG_BUILTIN_DTB_SOURCE_PATH="arch/riscv/boot/dts/boom.dts"|' .config
fi

echo "Step 3: Rebuild kernel with BOOM device tree"
make -j16 ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- 2>&1 | tail -50

echo ""
echo "Step 4: Rebuild BBL with updated kernel"
cd /home/ninadjangle/chipyard/toolchains/riscv-tools/riscv-pk/build

# Clean and reconfigure
make clean 2>/dev/null || true
../configure --host=riscv64-unknown-elf --with-payload=$KERNEL_PATH/vmlinux

make -j16 2>&1 | tail -50

if [ -f bbl ]; then
  echo "✓ BBL rebuilt successfully"
  ls -lh bbl
else
  echo "✗ BBL build failed"
  exit 1
fi

echo ""
echo "✓ BOOM device tree fix applied"
echo "Next: Run boot test with new kernel"
