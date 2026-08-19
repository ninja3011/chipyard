#!/bin/bash
# Build debug kernel with maximum output to trace hang point

set -e
cd /home/ninadjangle/chipyard

LINUX_SRC="software/firemarshal/boards/default/linux"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║        BUILDING DEBUG KERNEL FOR BOOM                     ║"
echo "║  Goal: Maximum verbosity to trace hang point              ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

cd "$LINUX_SRC"

echo "Creating debug kernel config..."
echo ""

# Aggressive debug config - prints EVERYTHING
cat > .config.debug << 'EOF'
# 64-bit RISC-V minimal
CONFIG_64BIT=y
CONFIG_RISCV=y
CONFIG_ARCH_RV64I=y
CONFIG_ARCH_MMAP_RND_BITS=16
CONFIG_AUDIT=n

# NO SMP - single core only
CONFIG_SMP=n
CONFIG_NR_CPUS=1

# Memory & paging
CONFIG_MMU=y
CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS=y
CONFIG_GENERIC_BUG=y

# Filesystem minimal
CONFIG_ROOTFS_INITRAMFS=y
CONFIG_TMPFS=y
CONFIG_BLK_DEV_INITRD=n

# Device tree - CRITICAL
CONFIG_OF=y
CONFIG_OF_EARLY_FLATTREE=y
CONFIG_OF_KOBJ=y

# Serial console - MAXIMUM DEBUG
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_SERIAL_OF_PLATFORM=y
CONFIG_PRINTK_TIME=y
CONFIG_PRINTK_CALLER=y
CONFIG_EARLY_PRINTK=y
CONFIG_DYNAMIC_DEBUG=y

# Scheduling
CONFIG_HZ=10
CONFIG_PREEMPTION=n
CONFIG_RCU_TRACE=n

# Debug output - AGGRESSIVE
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_INFO_REDUCED=n
CONFIG_PRINTK_LOGLEVEL_DEFAULT=7
CONFIG_DYNAMIC_DEBUG_CORE=y
CONFIG_TRACE_PRINTK=y
CONFIG_EARLY_PRINTK_USB=y

# Device initialization tracing
CONFIG_CMA=n
CONFIG_DEBUG_OBJECTS=y
CONFIG_DEBUG_OBJECTS_FREE=y
CONFIG_DEBUG_OBJECTS_SELFTEST=n

# Clock/Timer
CONFIG_GENERIC_CLOCKEVENTS=y
CONFIG_GENERIC_CLOCKEVENTS_BROADCAST=n

# Module support disabled
CONFIG_MODULES=n

# Panic on warn to catch issues
CONFIG_PANIC_ON_OOPS=y
CONFIG_PANIC_ON_OOPS_VALUE=1
CONFIG_PANIC_TIMEOUT=10

# Minimal networking (if needed)
CONFIG_NET=n
CONFIG_INET=n

# Minimal block layer
CONFIG_BLOCK=n

# Build flags
CONFIG_OPTIMIZE_INLINING=y
CONFIG_CC_OPTIMIZE_FOR_SIZE=n
EOF

echo "✅ Debug config created (.config.debug)"
echo ""
echo "Building kernel with debug enabled..."
echo ""

# Make with debug config
cp .config.debug .config
make oldconfig < /dev/null

echo ""
echo "Starting build (this takes 5-10 minutes)..."
echo ""

make -j16 2>&1 | tail -50

if [ -f vmlinux ]; then
  echo ""
  echo "✅ DEBUG KERNEL BUILD SUCCESSFUL!"
  echo "Binary: vmlinux"
  ls -lh vmlinux
  echo ""
  echo "Next: Rebuild BBL with this kernel"
  echo "  cd ../../toolchains/riscv-tools/riscv-pk/build"
  echo "  ../configure --host=riscv64-unknown-elf --with-payload=$(pwd)/vmlinux"
  echo "  make clean && make"
else
  echo ""
  echo "❌ Build failed - check output above"
  exit 1
fi
