#!/bin/bash

# AUTO FIX HANDLER
# Intelligently applies appropriate fix based on boot output

set -e

cd /home/ninadjangle/chipyard

BOOT_LOG="${1:-/tmp/linux-boot-full.log}"

if [ ! -f "$BOOT_LOG" ] || [ ! -s "$BOOT_LOG" ]; then
  echo "No boot output to analyze"
  exit 1
fi

echo "╔════════════════════════════════════════╗"
echo "║ AUTO FIX HANDLER                       ║"
echo "║ Analyzing boot output...               ║"
echo "╚════════════════════════════════════════╝"
echo ""

SIZE=$(wc -c < "$BOOT_LOG")
LINES=$(wc -l < "$BOOT_LOG")

echo "Boot log: $SIZE bytes, $LINES lines"
echo ""

# Decision logic
DECISION=""

# Check for success
if grep -qE "# |root@|login:|shell" "$BOOT_LOG"; then
  echo "✓✓✓ SHELL PROMPT DETECTED - MISSION ACCOMPLISHED! ✓✓✓"
  exit 0
fi

# Check for UART (milestone)
if grep -q "UART0 is here" "$BOOT_LOG"; then
  echo "✓ Kernel reaches UART initialization"

  # Check what comes after
  AFTER=$(grep -A 100 "UART0 is here" "$BOOT_LOG" | tail -99)
  AFTER_LINES=$(echo "$AFTER" | wc -l)

  echo "Lines after UART: $AFTER_LINES"

  if [ "$AFTER_LINES" -lt 3 ]; then
    echo ""
    echo "⚠ Kernel hangs immediately after UART"
    echo ">>> DIAGNOSIS: Device tree mismatch or console handshake"
    echo ">>> APPLYING: Device tree fix"
    DECISION="APPLY_DEVICE_TREE_FIX"
  else
    echo ""
    echo "Messages after UART detected"
    echo "$AFTER" | head -20
    echo ""
    echo ">>> DIAGNOSIS: Kernel progressing but needs debug"
    echo ">>> APPLYING: Debug output fix"
    DECISION="APPLY_DEBUG_FIX"
  fi
else
  echo "✗ No UART output - kernel not reaching console"

  # Check for kernel panic
  if grep -qi "panic\|oops" "$BOOT_LOG"; then
    echo ">>> DIAGNOSIS: Kernel panic"
    grep -i "panic\|oops" "$BOOT_LOG" | head -3
    echo ">>> APPLYING: Analyze panic and rebuild"
    DECISION="ANALYZE_PANIC"
  else
    echo ">>> DIAGNOSIS: Bootloader or memory issue"
    echo ">>> APPLYING: Bootloader check"
    DECISION="CHECK_BOOTLOADER"
  fi
fi

echo ""
echo "Decision: $DECISION"
echo ""

# Execute decision
case "$DECISION" in
  APPLY_DEVICE_TREE_FIX)
    echo "Applying device tree fix..."
    echo ""
    bash quick-fix-uart-hang.sh
    ;;

  APPLY_DEBUG_FIX)
    echo "Applying debug output fix..."
    echo ""
    bash apply-debug-output-fix.sh
    ;;

  ANALYZE_PANIC)
    echo "Kernel panic detected - analyzing..."
    grep -i "panic\|oops\|backtrace" "$BOOT_LOG" | head -50
    echo ""
    echo "TODO: Apply targeted panic fix"
    ;;

  CHECK_BOOTLOADER)
    echo "Checking bootloader configuration..."
    echo "TODO: Verify BBL and memory layout"
    ;;

  *)
    echo "Unknown decision: $DECISION"
    ;;
esac

echo ""
echo "✓ Fix handler complete"
