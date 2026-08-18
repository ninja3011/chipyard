#!/bin/bash
# Quick DOOM binary test - minimal simulator overhead

SIMULATOR="/home/ninadjangle/chipyard/sims/verilator/simulator-chipyard.harness-MediumBoomV3Config"
DOOM_BINARY="/home/ninadjangle/chipyard/doom3-port/output/doom.riscv"

echo "Quick DOOM test (100K cycles)..."
echo ""

# Try loading binary directly (faster than full Linux)
timeout 60 "$SIMULATOR" \
  +loadmem="$DOOM_BINARY" \
  +max-cycles=100000 \
  2>&1 | head -100

echo ""
echo "Quick test complete."
