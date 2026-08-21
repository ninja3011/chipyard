#!/bin/bash

# FIX: Enable maximum kernel debug output to see exactly where boot hangs

set -e

cd /home/ninadjangle/chipyard

echo "=========================================="
echo "FIX: Enable Kernel Debug Output"
echo "=========================================="

KERNEL_PATH="./software/firemarshal/boards/default/linux"

echo "Step 1: Enable debug kernel options"
cd "$KERNEL_PATH"

# Enable verbose logging
cat >> .config << 'EOF'
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_DRIVER=y
CONFIG_DEBUG_DEVICES=y
CONFIG_DYNAMIC_DEBUG=y
CONFIG_DYNAMIC_DEBUG_CORE=y
CONFIG_PRINTK_TIME=y
CONFIG_PRINTK_CALLER=y
CONFIG_EARLY_PRINTK=y
CONFIG_DEBUG_EARLY_PRINT=y
CONFIG_SERIAL_EARLYCON=y
CONFIG_CONSOLE_LOGLEVEL_DEFAULT=15
CONFIG_MESSAGE_LOGLEVEL_DEFAULT=7
EOF

echo "Step 2: Rebuild kernel"
make -j16 ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- 2>&1 | tail -50

echo "Step 3: Rebuild BBL with debug kernel"
cd /home/ninadjangle/chipyard/toolchains/riscv-tools/riscv-pk/build

make clean 2>/dev/null || true
../configure --host=riscv64-unknown-elf --with-payload=$KERNEL_PATH/vmlinux
make -j16 2>&1 | tail -50

echo ""
echo "✓ Debug output enabled"
echo "Next: Run boot test - should see much more output"
