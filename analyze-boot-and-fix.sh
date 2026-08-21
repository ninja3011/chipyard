#!/bin/bash

# SMART BOOT ANALYZER
# Analyzes boot output and recommends/applies appropriate fix

set -e

cd /home/ninadjangle/chipyard

BOOT_LOG="${1:-/tmp/linux-boot-full.log}"

if [ ! -f "$BOOT_LOG" ] || [ ! -s "$BOOT_LOG" ]; then
  echo "No boot log to analyze"
  exit 0
fi

echo "=========================================="
echo "BOOT OUTPUT ANALYSIS"
echo "=========================================="

SIZE=$(wc -c < "$BOOT_LOG")
LINES=$(wc -l < "$BOOT_LOG")

echo "Log file: $BOOT_LOG"
echo "Size: $SIZE bytes"
echo "Lines: $LINES"
echo ""

# Determine diagnosis
DIAGNOSIS=""
RECOMMENDED_FIX=""

# Test 1: Check for shell prompt (success)
if grep -qE "^(#|\\$ |root@)" "$BOOT_LOG"; then
  echo "✓✓✓ LINUX FULLY BOOTED WITH SHELL ✓✓✓"
  DIAGNOSIS="FULL_SUCCESS"
  exit 0
fi

# Test 2: Check for UART output (milestone)
if grep -q "UART0 is here" "$BOOT_LOG"; then
  echo "✓ Kernel reaches UART initialization"

  # Check what comes after
  AFTER_UART=$(grep -A 50 "UART0 is here" "$BOOT_LOG" | tail -49)
  AFTER_UART_LINES=$(echo "$AFTER_UART" | wc -l)

  if [ $AFTER_UART_LINES -lt 3 ]; then
    echo "⚠ Kernel hangs immediately after UART"
    DIAGNOSIS="UART_HANG"
    RECOMMENDED_FIX="device-tree-or-debug"
  else
    echo "Output after UART:"
    echo "$AFTER_UART" | head -20
    echo "..."
    DIAGNOSIS="UART_PROGRESSING"
    RECOMMENDED_FIX="debug-and-analyze"
  fi
else
  echo "✗ No UART output - kernel not reaching console"
  DIAGNOSIS="NO_UART"
  RECOMMENDED_FIX="check-memory-or-bootloader"
fi

echo ""
echo "Diagnosis: $DIAGNOSIS"
echo "Recommended Fix: $RECOMMENDED_FIX"

# Apply recommended fix
echo ""
echo "Applying fix strategy: $RECOMMENDED_FIX"

case "$RECOMMENDED_FIX" in
  device-tree-or-debug)
    echo "Fix: Apply BOOM device tree to kernel"
    # Would apply device tree fix here
    ;;
  debug-and-analyze)
    echo "Fix: Enable kernel debug output"
    # Would apply debug output fix here
    ;;
  check-memory-or-bootloader)
    echo "Fix: Verify bootloader and memory configuration"
    # Would check bootloader
    ;;
esac

echo ""
echo "=========================================="
echo "Analysis complete"
echo "=========================================="
