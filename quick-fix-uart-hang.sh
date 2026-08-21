#!/bin/bash

# QUICK FIX FOR UART HANG
# If kernel prints UART message and hangs, apply this immediately

set -e

cd /home/ninadjangle/chipyard

echo "=========================================="
echo "QUICK FIX: UART HANG - Apply Device Tree"
echo "=========================================="

# Step 1: Get BOOM device tree
BOOM_DTS="./sims/verilator/generated-src/chipyard.harness.TestHarness.MediumBoomV3Config/chipyard.harness.TestHarness.MediumBoomV3Config.dts"

if [ ! -f "$BOOM_DTS" ]; then
  echo "ERROR: BOOM device tree not found"
  exit 1
fi

echo "Step 1: Copying BOOM device tree..."
cp "$BOOM_DTS" /tmp/boom.dts
ls -lh /tmp/boom.dts

# Step 2: Check current kernel sources
echo ""
echo "Step 2: Checking kernel..."
KERNEL_PATH="./software/firemarshal/boards/default/linux"

if [ ! -f "$KERNEL_PATH/vmlinux" ]; then
  echo "Need to rebuild kernel with BOOM device tree"
  cd "$KERNEL_PATH"

  # Activate toolchain
  source ../../../../../../scripts/conda/conda-env-activate.sh env 2>/dev/null || true

  # Quick config update to use BOOM device tree
  echo "Adding BOOM device tree to kernel config..."

  # Enable device tree support if not already
  if ! grep -q "CONFIG_OF=y" .config; then
    echo "CONFIG_OF=y" >> .config
    echo "CONFIG_OF_FLATTREE=y" >> .config
  fi

  echo "CONFIG_DTB_SOURCE_FILE=\"/tmp/boom.dts\"" >> .config

  echo "Building kernel..."
  make -j16 ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- 2>&1 | tail -30

  if [ ! -f vmlinux ]; then
    echo "Kernel build failed"
    exit 1
  fi
fi

# Step 3: Rebuild BBL with updated kernel
echo ""
echo "Step 3: Rebuilding BBL..."

cd /home/ninadjangle/chipyard/toolchains/riscv-tools/riscv-pk/build

rm -f bbl
make clean 2>/dev/null || true

echo "Configuring..."
../configure --host=riscv64-unknown-elf --with-payload=$KERNEL_PATH/vmlinux 2>&1 | tail -10

echo "Building..."
make -j16 2>&1 | tail -20

if [ ! -f bbl ]; then
  echo "BBL build failed"
  exit 1
fi

echo ""
echo "✓ QUICK FIX COMPLETE"
echo "New BBL with BOOM device tree: $PWD/bbl"
echo ""
echo "Next: Run boot test with:"
echo "  timeout 2000 sims/verilator/simulator-chipyard.harness-MediumBoomV3Config +permissive +max-cycles=20000000000 +permissive-off $PWD/bbl"
