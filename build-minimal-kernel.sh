#!/bin/bash
# Build minimal Linux kernel for BOOM test harness debugging
# Strip down to essentials: serial console + basic boot

set -e
cd /home/ninadjangle/chipyard

echo "=== Building Minimal RV64 Linux Kernel ==="
echo ""

# Check if kernel source available
LINUX_SRC="software/firemarshal/boards/default/linux"

if [ ! -d "$LINUX_SRC" ]; then
  echo "❌ Linux source not found at $LINUX_SRC"
  exit 1
fi

cd "$LINUX_SRC"

echo "Current config:"
head -20 .config

echo ""
echo "Making minimal config..."

# Minimal kernel config for BOOM test harness
cat > /tmp/minimal.config << 'EOF'
CONFIG_64BIT=y
CONFIG_RISCV=y
CONFIG_ARCH_RV64I=y
CONFIG_MMU=y
CONFIG_SMP=n
CONFIG_HZ=10
CONFIG_PREEMPTION=n
CONFIG_HAVE_ARCH_KGDB=y

# Serial console (critical!)
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_SERIAL_OF_PLATFORM=y
CONFIG_PRINTK_TIME=y
CONFIG_EARLY_PRINTK=y
CONFIG_RISCV_SBI_V01=y

# Device tree
CONFIG_OF=y
CONFIG_OF_EARLY_FLATTREE=y
CONFIG_OF_RESERVED_MEMORY=y

# Filesystem (minimal)
CONFIG_EXT2_FS=y
CONFIG_EXT2_FS_POSIX_ACL=n
CONFIG_TMPFS=y
CONFIG_ROOTFS_INITRAMFS=y

# Debug output
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_INFO=y
CONFIG_PRINTK_LOGLEVEL_DEFAULT=7
EOF

echo "✅ Minimal config created"
echo ""
echo "To rebuild kernel with this config:"
echo "  cp /tmp/minimal.config $LINUX_SRC/.config"
echo "  cd $LINUX_SRC"
echo "  make menuconfig (or make oldconfig)"
echo "  make -j16"
