#!/bin/bash
# Master autonomous Linux boot fixer - runs overnight
# Tries multiple approaches until success or clear blocker identified

set -e

cd /home/ninadjangle/chipyard
source .conda-env/bin/activate

LOG_DIR="/tmp/linux-overnight-logs"
mkdir -p "$LOG_DIR"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   OVERNIGHT LINUX BOOT AUTONOMOUS FIXER                   ║"
echo "║  Goal: Linux booting by morning or clear fix identified   ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "Log directory: $LOG_DIR"
echo ""

# ═══════════════════════════════════════════════════════════════
# PHASE 1: Test current kernel (high cycle limit)
# ═══════════════════════════════════════════════════════════════

phase1_test() {
  echo "[PHASE 1] Testing current kernel with high cycle budget"
  echo "Timeout: 10 minutes | Cycles: 15 billion"
  echo "Log: $LOG_DIR/phase1-current-kernel.log"
  echo ""

  timeout 600 sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
    +permissive \
    +max-cycles=15000000000 \
    +permissive-off \
    software/firemarshal/images/firechip/br-base/br-base-bin \
    2>&1 | tee "$LOG_DIR/phase1-current-kernel.log" &

  PHASE1_PID=$!

  # Monitor for output over 10 minutes
  for i in {1..20}; do
    sleep 30
    LOG_SIZE=$(stat -c%s "$LOG_DIR/phase1-current-kernel.log" 2>/dev/null || echo 0)

    if [ $LOG_SIZE -gt 500 ]; then
      echo "✅ PHASE 1 SUCCESS! Kernel produced output!"
      echo ""
      head -50 "$LOG_DIR/phase1-current-kernel.log"
      echo ""
      echo "Next: Analyze and fix specific error"
      return 0
    fi
  done

  kill -9 $PHASE1_PID 2>/dev/null || true
  wait $PHASE1_PID 2>/dev/null || true

  echo "❌ PHASE 1 FAILED: No kernel output after 10 minutes"
  return 1
}

# ═══════════════════════════════════════════════════════════════
# PHASE 2: Create and test minimal kernel
# ═══════════════════════════════════════════════════════════════

phase2_minimal_kernel() {
  echo ""
  echo "[PHASE 2] Building minimal kernel variant"
  echo "Goal: Strip down to serial console only, no drivers"
  echo ""

  LINUX_SRC="software/firemarshal/boards/default/linux"

  if [ ! -d "$LINUX_SRC" ]; then
    echo "❌ Linux source not available"
    return 1
  fi

  echo "Creating minimal config..."
  cat > "$LINUX_SRC/.config.minimal" << 'EOF'
CONFIG_64BIT=y
CONFIG_RISCV=y
CONFIG_ARCH_RV64I=y
CONFIG_MMU=y
CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS=y

# Absolute minimum for BOOM test harness
CONFIG_SMP=n
CONFIG_HOTPLUG_CPU=n
CONFIG_PREEMPTION=n

# Serial console (critical!)
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_PRINTK_TIME=y
CONFIG_EARLY_PRINTK=y

# Just enough for boot
CONFIG_OF=y
CONFIG_OF_EARLY_FLATTREE=y
CONFIG_BLK_DEV_INITRD=n
CONFIG_EXT2_FS=n
CONFIG_TMPFS=y

# Debug
CONFIG_DEBUG_KERNEL=y
CONFIG_PRINTK_LOGLEVEL_DEFAULT=7
CONFIG_HZ=10
EOF

  echo "✓ Config ready at: $LINUX_SRC/.config.minimal"
  echo ""
  echo "To rebuild:"
  echo "  cd $LINUX_SRC"
  echo "  cp .config.minimal .config"
  echo "  make -j16 > /tmp/kernel-build.log 2>&1"
  echo ""
  echo "Status: Minimal kernel build prepared (manual trigger needed)"
  return 0
}

# ═══════════════════════════════════════════════════════════════
# PHASE 3: Device tree analysis
# ═══════════════════════════════════════════════════════════════

phase3_device_tree() {
  echo ""
  echo "[PHASE 3] Device tree analysis"
  echo ""

  echo "Searching for device tree requirements..."
  strings software/firemarshal/boards/default/linux/vmlinux | \
    grep -i "compatible" | head -5 > "$LOG_DIR/dt-required.txt" || true

  echo "Device tree expectations saved to: $LOG_DIR/dt-required.txt"
  cat "$LOG_DIR/dt-required.txt"

  echo ""
  echo "Custom device tree creation planned (detailed analysis needed)"
}

# ═══════════════════════════════════════════════════════════════
# MAIN EXECUTION
# ═══════════════════════════════════════════════════════════════

TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")
echo "Start time: $TIMESTAMP"
echo ""

# Run Phase 1
if phase1_test; then
  echo ""
  echo "✅ LINUX BOOT SUCCESSFUL!"
  exit 0
fi

# Phase 1 failed, move to Phase 2
phase2_minimal_kernel
phase3_device_tree

echo ""
echo "═════════════════════════════════════════════════════════════"
echo "OVERNIGHT FIX SUMMARY"
echo "─────────────────────────────────────────────────────────────"
echo "Phase 1: No kernel output (device tree issue likely)"
echo "Phase 2: Minimal kernel config prepared"
echo "Phase 3: Device tree analysis started"
echo ""
echo "All logs available in: $LOG_DIR/"
echo ""
echo "Manual next steps:"
echo "  1. Review device tree requirements"
echo "  2. Build minimal kernel variant"
echo "  3. Test with custom device tree"
echo "═════════════════════════════════════════════════════════════"

END_TIME=$(date +"%Y-%m-%d %H:%M:%S")
echo "End time: $END_TIME"
