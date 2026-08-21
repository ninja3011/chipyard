#!/bin/bash

# AGGRESSIVE LINUX FIX STRATEGY
# Tries multiple fixes rapidly when standard boot fails

set -e

cd /home/ninadjangle/chipyard

BOOT_LOG="/tmp/linux-boot-full.log"

echo "=========================================="
echo "AGGRESSIVE LINUX BOOT FIX"
echo "=========================================="

if [ ! -f "$BOOT_LOG" ] || [ ! -s "$BOOT_LOG" ]; then
  echo "No boot output yet. Waiting..."
  exit 0
fi

echo "Analyzing boot output..."
LINES=$(wc -l < "$BOOT_LOG")
echo "Log has $LINES lines"

# Strategy 1: If kernel reaches UART but no progress, try simpler kernel
if grep -q "UART0 is here" "$BOOT_LOG" && [ $LINES -lt 100 ]; then
  echo ""
  echo "Strategy 1: Kernel reaches UART but hangs"
  echo "Applying fix: Use minimal kernel config"

  KERNEL_PATH="./software/firemarshal/boards/default/linux"
  cd "$KERNEL_PATH"

  # Create ultra-minimal config
  cat > .config << 'EOF'
CONFIG_64BIT=y
CONFIG_RISCV=y
CONFIG_SMP=y
CONFIG_NR_CPUS=1
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_CONSOLE_ON_UART=y
CONFIG_EARLY_PRINTK=y
CONFIG_PRINTK=y
CONFIG_RD_GZIP=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_TMPFS=y
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
CONFIG_DEVTMPFS=y
CONFIG_UNIX=y
EOF

  echo "Building minimal kernel..."
  make -j16 ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- 2>&1 | tail -20

  echo "Rebuilding BBL..."
  cd /home/ninadjangle/chipyard/toolchains/riscv-tools/riscv-pk/build
  make clean 2>/dev/null || true
  ../configure --host=riscv64-unknown-elf --with-payload=$KERNEL_PATH/vmlinux
  make -j16 2>&1 | tail -20

  if [ -f bbl ]; then
    echo "✓ Minimal kernel built"
    cp bbl /tmp/bbl-minimal
  fi
fi

# Strategy 2: If no UART output at all, try different memory layout
if ! grep -q "UART0" "$BOOT_LOG"; then
  echo ""
  echo "Strategy 2: No UART output - possible memory/device tree issue"
  echo "Trying: Copy test harness device tree into kernel"

  KERNEL_PATH="./software/firemarshal/boards/default/linux"
  BOOM_DTS="./sims/verilator/generated-src/chipyard.harness.TestHarness.MediumBoomV3Config/chipyard.harness.TestHarness.MediumBoomV3Config.dts"

  if [ -f "$BOOM_DTS" ]; then
    cp "$BOOM_DTS" "$KERNEL_PATH/arch/riscv/boot/dts/boom.dts"
    echo "✓ Device tree copied"
  fi
fi

echo ""
echo "✓ Fixes applied"
echo "Next: Rebuild and test again"
