#!/bin/bash

# AUTONOMOUS LINUX BOOT LOOP
# Runs until morning 8 AM or until Linux boots successfully

set -e

cd /home/ninadjangle/chipyard

DEADLINE="08:00"  # Stop at 8 AM
ITERATION=0
MAX_ITERATIONS=20
SUCCESS=false

echo "=========================================="
echo "AUTONOMOUS LINUX BOOT ITERATION LOOP"
echo "Started: $(date)"
echo "Running until: $DEADLINE or Linux boots"
echo "=========================================="

while true; do
  CURRENT_TIME=$(date +%H:%M)

  # Check deadline
  if [[ "$CURRENT_TIME" > "$DEADLINE" ]] || [[ "$CURRENT_TIME" == "$DEADLINE" ]]; then
    echo "Reached deadline (8 AM). Stopping."
    break
  fi

  ITERATION=$((ITERATION + 1))
  echo ""
  echo "====== ITERATION $ITERATION - $(date) ======"

  # Run boot test
  BOOT_LOG="/tmp/linux-boot-iter-$ITERATION.log"
  echo "Running boot test (timeout 2000s, output to $BOOT_LOG)..."

  timeout 2000 sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
    +permissive +max-cycles=20000000000 +permissive-off \
    software/firemarshal/images/firechip/br-base/br-base-bin \
    2>&1 | tee "$BOOT_LOG" || true

  # Analyze output
  echo ""
  echo "--- Analyzing boot output ---"

  if [ ! -f "$BOOT_LOG" ] || [ ! -s "$BOOT_LOG" ]; then
    echo "✗ No output generated"
    echo "Will retry in next iteration"
  else
    echo "Boot log size: $(wc -c < $BOOT_LOG) bytes"

    # Check for success indicators
    if grep -q "# " "$BOOT_LOG" || grep -q "root@" "$BOOT_LOG" || grep -q "shell" "$BOOT_LOG"; then
      echo "✓✓✓ SHELL PROMPT DETECTED - LINUX BOOTED SUCCESSFULLY! ✓✓✓"
      echo ""
      tail -50 "$BOOT_LOG"
      SUCCESS=true
      break
    fi

    # Check for key milestones
    if grep -q "UART0 is here" "$BOOT_LOG"; then
      echo "✓ Kernel reaches UART"

      # Show everything after UART
      echo "Output after UART:"
      grep -A 100 "UART0 is here" "$BOOT_LOG" | head -50
    fi

    # Check for errors
    if grep -qi "panic\|oops\|error\|failed\|cannot find" "$BOOT_LOG"; then
      echo "✗ Kernel error detected:"
      grep -i "panic\|oops\|error\|failed\|cannot find" "$BOOT_LOG" | head -10
    fi

    # Copy best log
    if [ $ITERATION -eq 1 ]; then
      cp "$BOOT_LOG" /tmp/linux-boot-best.log
    else
      PREV_SIZE=$(wc -c < /tmp/linux-boot-best.log 2>/dev/null || echo 0)
      CURR_SIZE=$(wc -c < "$BOOT_LOG" 2>/dev/null || echo 0)
      if [ "$CURR_SIZE" -gt "$PREV_SIZE" ]; then
        cp "$BOOT_LOG" /tmp/linux-boot-best.log
        echo "Saved as best log (larger output)"
      fi
    fi
  fi

  # Decide next action
  if [ $ITERATION -ge $MAX_ITERATIONS ]; then
    echo "Reached max iterations, stopping"
    break
  fi

  echo ""
  echo "Preparing next iteration..."

  # For now, just retry as-is to see if we get consistent output
  # TODO: Apply targeted fixes based on error analysis

  sleep 60

done

echo ""
echo "=========================================="
if [ "$SUCCESS" = true ]; then
  echo "✓ SUCCESS: LINUX FULLY BOOTED"
else
  echo "⚠ Session ended - Linux not yet booting"
  echo "Best output in: /tmp/linux-boot-best.log"
fi
echo "Completed: $(date)"
echo "=========================================="
