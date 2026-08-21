#!/bin/bash

# AUTO-FIX LINUX BOOT ISSUES - Autonomous Session
# Runs iteratively to fix hang points

set -e

cd /home/ninadjangle/chipyard

ATTEMPT=1
MAX_ATTEMPTS=10
BOOT_LOG="/tmp/linux-boot-full.log"

while [ $ATTEMPT -le $MAX_ATTEMPTS ]; do
  echo ""
  echo "=========================================="
  echo "FIX ATTEMPT #$ATTEMPT - $(date)"
  echo "=========================================="

  # Check if we already have output from last boot test
  if [ -f "$BOOT_LOG" ] && [ -s "$BOOT_LOG" ]; then
    echo "Analyzing previous boot output..."

    # Look for known hang points
    if grep -q "UART0 is here" "$BOOT_LOG"; then
      echo "✓ Kernel reaches UART initialization"

      # Check what comes after UART message
      LINES_AFTER=$(grep -A 50 "UART0 is here" "$BOOT_LOG" | wc -l)
      echo "Lines of output after UART: $LINES_AFTER"

      if grep -q "cannot find" "$BOOT_LOG"; then
        echo "✗ Device not found - applying device tree fix..."
        ATTEMPT=$((MAX_ATTEMPTS + 1))  # Go to device tree fix
        break
      fi

      if grep -q "Kernel panic\|oops\|error\|failed" "$BOOT_LOG"; then
        echo "✗ Kernel error detected - checking for specific issues..."
        grep "panic\|oops\|error\|failed" "$BOOT_LOG" | head -5
      fi

      # If just silence after UART, likely waiting for something
      if [ $LINES_AFTER -lt 5 ]; then
        echo "⚠ Kernel hangs silently after UART - likely waiting for device or input"
      fi
    fi

    echo "Full boot output:"
    head -100 "$BOOT_LOG"
    echo ""
    echo "... (showing first 100 lines)"
  else
    echo "Waiting for boot test to produce output..."
    echo "Simulator status:"
    ps aux | grep "MediumBoomV3Config" | grep -v grep | head -2
  fi

  echo ""
  echo "Next action: Monitor for output completion"
  ATTEMPT=$((ATTEMPT + 1))

  if [ $ATTEMPT -le $MAX_ATTEMPTS ]; then
    sleep 120
  fi
done

echo ""
echo "=========================================="
echo "ANALYSIS COMPLETE"
echo "=========================================="
