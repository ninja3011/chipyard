#!/bin/bash
# Boot DOOM binary on BOOM simulator with Linux
# Note: This can take 5-15 minutes for Linux to boot in RTL simulation

set -e

SIMULATOR="/home/ninadjangle/chipyard/sims/verilator/simulator-chipyard.harness-MediumBoomV3Config"
LINUX_IMAGE="/home/ninadjangle/chipyard/software/firemarshal/images/firechip/br-base/br-base-bin"
DOOM_BINARY="/home/ninadjangle/chipyard/doom3-port/output/doom.riscv"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  DOOM on RISC-V - BOOM CPU Simulator Boot                  ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "Configuration:"
echo "  Simulator: MediumBoomV3Config (4-wide, 8-stage)"
echo "  CPU: BOOM (out-of-order, RV64IMAFDv)"
echo "  ISA: RISC-V 64-bit"
echo "  Linux: BuildRoot minimal (br-base)"
echo "  Test Binary: doom.riscv (100 KB)"
echo ""
echo "WARNING: Verilator RTL simulation is slow!"
echo "Expected boot time: 5-15 minutes"
echo ""
echo "Starting simulator..."
echo ""

# Run simulator with Linux kernel
# The +loadmem option loads the binary, +max-cycles limits runtime
timeout 1200 "$SIMULATOR" "$LINUX_IMAGE" 2>&1 | tee /tmp/doom_simulator.log

echo ""
echo "Simulator execution completed or timed out"
echo "Output saved to: /tmp/doom_simulator.log"
