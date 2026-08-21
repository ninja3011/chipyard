#!/bin/bash

# ROBUST KERNEL REBUILD
# Handles known issues: fence.i errors, missing tools, etc.

set -e

cd /home/ninadjangle/chipyard

echo "=========================================="
echo "ROBUST KERNEL REBUILD"
echo "=========================================="

KERNEL_PATH="./software/firemarshal/boards/default/linux"

# Step 1: Activate toolchain
echo "Step 1: Activating RISC-V toolchain..."
source scripts/conda/conda-env-activate.sh env

# Verify tools are available
if ! command -v riscv64-unknown-elf-gcc &> /dev/null; then
  echo "✗ RISC-V GCC not in PATH"
  export PATH="/home/ninadjangle/chipyard/.conda-env/bin:$PATH"
fi

echo "Using toolchain:"
riscv64-unknown-elf-gcc --version | head -1
riscv64-unknown-elf-gcc -print-search-dirs | grep install

# Step 2: Clean kernel
echo "Step 2: Cleaning kernel..."
cd "$KERNEL_PATH"
make clean ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- 2>&1 | tail -5

# Step 3: Make kernel
echo "Step 3: Building kernel (this may take 5-10 minutes)..."
MAKE_START=$(date +%s)

# Build with error handling
if make -j16 ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- 2>&1 | tee /tmp/kernel-build.log; then
  echo "✓ Kernel built successfully"
else
  BUILD_ERROR=$(tail -20 /tmp/kernel-build.log | grep -i "error\|failed" || true)
  echo "✗ Kernel build failed:"
  echo "$BUILD_ERROR"

  # Try to handle fence.i error
  if grep -q "fence.i" /tmp/kernel-build.log; then
    echo "  Handling fence.i error..."
    make -j16 ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- CFLAGS="-march=rv64imafd_zifencei" 2>&1 | tail -20
  fi
fi

MAKE_END=$(date +%s)
MAKE_TIME=$((MAKE_END - MAKE_START))
echo "Kernel build time: ${MAKE_TIME}s"

# Step 4: Verify vmlinux exists
if [ ! -f vmlinux ]; then
  echo "✗ vmlinux not found after build"
  exit 1
fi

echo "vmlinux size: $(ls -lh vmlinux | awk '{print $5}')"

# Step 5: Rebuild BBL with new kernel
echo ""
echo "Step 5: Rebuilding BBL with new kernel..."

cd /home/ninadjangle/chipyard/toolchains/riscv-tools/riscv-pk/build

# Clean old build
rm -f bbl
make clean 2>/dev/null || true

# Reconfigure
echo "Configuring BBL..."
../configure --host=riscv64-unknown-elf --with-payload=$KERNEL_PATH/vmlinux 2>&1 | tail -10

# Build
echo "Building BBL..."
if make -j16 2>&1 | tee /tmp/bbl-build.log; then
  echo "✓ BBL built successfully"
else
  BBL_ERROR=$(tail -20 /tmp/bbl-build.log | grep -i "error\|failed" || true)
  echo "✗ BBL build failed:"
  echo "$BBL_ERROR"
  exit 1
fi

# Verify
if [ ! -f bbl ]; then
  echo "✗ bbl not found after build"
  exit 1
fi

echo "BBL size: $(ls -lh bbl | awk '{print $5}')"

echo ""
echo "=========================================="
echo "✓ KERNEL AND BBL REBUILT SUCCESSFULLY"
echo "=========================================="
echo ""
echo "Next: Run boot test with: timeout 2000 sims/verilator/simulator-chipyard.harness-MediumBoomV3Config +permissive +max-cycles=20000000000 +permissive-off $PWD/bbl"
