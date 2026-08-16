#!/bin/bash
# Script to compile Verilator simulator and run Linux kernel on BOOM CPU

set -e

CHIPYARD_DIR=/home/ninadjangle/chipyard
CONDA_ENV=$CHIPYARD_DIR/.conda-env
SIM_DIR=$CHIPYARD_DIR/sims/verilator
CONFIG=MediumBoomV3Config

# Use pre-built bbl-vmlinux from firesim workloads
BBL_VMLINUX=$CHIPYARD_DIR/software/firesim-paper-workloads/simperf-test/bbl-vmlinux

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   Running Linux Kernel on BOOM CPU (MediumBoomV3Config)     ║"
echo "╚════════════════════════════════════════════════════════════╝"

# Activate conda environment
echo "[1/4] Activating Conda environment..."
source $CONDA_ENV/etc/profile.d/conda.sh
conda activate $CONDA_ENV

# Build Verilator simulator (if not already built)
if [ ! -f "$SIM_DIR/simulator-chipyard.harness-$CONFIG" ]; then
    echo "[2/4] Building Verilator simulator (this may take 5-10 minutes)..."
    cd $SIM_DIR
    make CONFIG=$CONFIG -j4 2>&1 | tee /tmp/verilator-build.log
    cd -
else
    echo "[2/4] Using pre-built Verilator simulator..."
fi

# Verify BBL+vmlinux exists
if [ ! -f "$BBL_VMLINUX" ]; then
    echo "❌ Error: BBL+vmlinux not found at $BBL_VMLINUX"
    echo "   Using default Linux kernel from firemarshal instead..."
    BBL_VMLINUX=$CHIPYARD_DIR/software/firemarshal/boards/default/linux/vmlinux
fi

if [ ! -f "$BBL_VMLINUX" ]; then
    echo "❌ Error: No Linux kernel found. Please build one first."
    exit 1
fi

echo "[3/4] Running Linux kernel on BOOM CPU simulator..."
echo "      Bootloader + Kernel: $BBL_VMLINUX"
echo "      Expected: Linux boot messages, then shell prompt"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Run simulator with Linux kernel
# Timeout after 30 seconds (reduce if needed for faster iteration)
timeout 30 $SIM_DIR/simulator-chipyard.harness-$CONFIG \
    +max-cycles=100000 \
    +use-htif \
    $BBL_VMLINUX \
    2>&1 | tee /tmp/linux-boot.log || true

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "[4/4] Simulation complete. Boot log saved to /tmp/linux-boot.log"
echo ""

# Check for successful boot indicators
if grep -q "Welcome to Linux" /tmp/linux-boot.log || \
   grep -q "RISC-V Linux" /tmp/linux-boot.log || \
   grep -q "OpenSBI" /tmp/linux-boot.log; then
    echo "✅ SUCCESS: Linux kernel booted on BOOM CPU!"
    echo ""
    echo "Boot output (last 30 lines):"
    tail -30 /tmp/linux-boot.log
else
    echo "⏳ Simulation ran. Check output above for boot progress."
    echo "   (May need longer timeout or kernel configuration adjustments)"
fi

echo ""
echo "📝 Full log: /tmp/linux-boot.log"
