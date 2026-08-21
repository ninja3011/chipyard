#!/bin/bash

# MASTER ORCHESTRATION SCRIPT
# Fully autonomous Linux boot loop until 8 AM or success

set -e

cd /home/ninadjangle/chipyard

DEADLINE_HOUR=8
CURRENT_HOUR=$(date +%H)
ITERATION=0
MAX_ITERATIONS=30

# Helper function to check deadline
check_deadline() {
  CURRENT_HOUR=$(date +%H)
  if [ "$CURRENT_HOUR" -ge "$DEADLINE_HOUR" ]; then
    return 0  # Deadline reached
  fi
  return 1  # Keep going
}

# Helper to wait for boot output
wait_for_boot() {
  local LOG_FILE="$1"
  local TIMEOUT=2100

  echo "Waiting for boot output (up to $TIMEOUT seconds)..."

  local ELAPSED=0
  while [ $ELAPSED -lt $TIMEOUT ]; do
    if [ -f "$LOG_FILE" ] && [ -s "$LOG_FILE" ]; then
      echo "✓ Boot output received!"
      return 0
    fi

    # Check if simulator still running
    if ! pgrep -f "MediumBoomV3Config.*br-base-bin" > /dev/null; then
      echo "⚠ Simulator exited"
      if [ -f "$LOG_FILE" ]; then
        cat "$LOG_FILE"
      fi
      return 1
    fi

    if [ $((ELAPSED % 300)) -eq 0 ] && [ $ELAPSED -gt 0 ]; then
      SIZE=$(wc -c < "$LOG_FILE" 2>/dev/null || echo 0)
      echo "[$(date +'%H:%M')] Still waiting ($ELAPSED/$TIMEOUT sec, log: $SIZE bytes)..."
    fi

    sleep 10
    ELAPSED=$((ELAPSED + 10))
  done

  echo "⚠ Timeout waiting for boot output"
  return 1
}

# Main loop
echo "=========================================="
echo "MASTER AUTONOMOUS LINUX BOOT LOOP"
echo "Start: $(date)"
echo "Deadline: $(date -d "$(date +%Y-%m-%d) $DEADLINE_HOUR:00" +'%H:%M')"
echo "=========================================="

while true; do
  # Check deadline
  if check_deadline; then
    echo "Deadline reached (8 AM). Stopping."
    break
  fi

  ITERATION=$((ITERATION + 1))
  echo ""
  echo "====== ITERATION $ITERATION - $(date +'%H:%M:%S') ======"

  BOOT_LOG="/tmp/linux-boot-iter-$ITERATION.log"

  # Run boot test
  echo "Starting boot test (output to $BOOT_LOG)..."
  timeout 2100 sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
    +permissive +max-cycles=20000000000 +permissive-off \
    software/firemarshal/images/firechip/br-base/br-base-bin \
    2>&1 > "$BOOT_LOG" &
  BOOT_PID=$!

  # Wait for output
  wait_for_boot "$BOOT_LOG"
  BOOT_RESULT=$?

  # Kill simulator if still running
  kill $BOOT_PID 2>/dev/null || true
  wait $BOOT_PID 2>/dev/null || true

  echo ""
  echo "--- Boot Test Analysis ---"

  if [ -f "$BOOT_LOG" ] && [ -s "$BOOT_LOG" ]; then
    echo "Log size: $(wc -c < $BOOT_LOG) bytes"
    echo "First 50 lines:"
    head -50 "$BOOT_LOG"

    # Check for success
    if grep -q "# " "$BOOT_LOG" || grep -q "root@" "$BOOT_LOG" || grep -q "login:" "$BOOT_LOG"; then
      echo ""
      echo "✓✓✓ LINUX FULLY BOOTED - SHELL DETECTED ✓✓✓"
      echo ""
      tail -100 "$BOOT_LOG"
      echo ""
      echo "SUCCESS at $(date)"
      break
    fi

    # Check for UART message
    if grep -q "UART0 is here" "$BOOT_LOG"; then
      echo "✓ Kernel reaches UART"

      # Show output after UART
      AFTER_UART=$(grep -A 50 "UART0 is here" "$BOOT_LOG" | tail -45)
      echo "Output after UART:"
      echo "$AFTER_UART"

      # Check what comes next
      if [ -z "$AFTER_UART" ] || [ $(echo "$AFTER_UART" | wc -l) -lt 2 ]; then
        echo "⚠ Kernel hangs after UART initialization"
        echo "Need to debug device initialization"
      fi
    else
      echo "✗ No UART output - kernel may not be reaching console"
    fi

    # Check for errors
    if grep -qi "panic\|oops\|error.*failed\|cannot find" "$BOOT_LOG"; then
      echo "✗ Kernel error detected:"
      grep -i "panic\|oops\|error.*failed\|cannot find" "$BOOT_LOG" | head -5
    fi
  else
    echo "✗ No boot output generated"
  fi

  # Copy as best log if it has more output
  if [ $ITERATION -eq 1 ]; then
    cp "$BOOT_LOG" /tmp/linux-boot-best.log
  else
    PREV_SIZE=$(wc -c < /tmp/linux-boot-best.log 2>/dev/null || echo 0)
    CURR_SIZE=$(wc -c < "$BOOT_LOG" 2>/dev/null || echo 0)
    if [ "$CURR_SIZE" -gt "$PREV_SIZE" ]; then
      cp "$BOOT_LOG" /tmp/linux-boot-best.log
    fi
  fi

  # Prepare for next iteration
  if [ $ITERATION -ge $MAX_ITERATIONS ]; then
    echo "Reached max iterations"
    break
  fi

  # Brief pause before next test
  echo "Waiting 60 seconds before next iteration..."
  sleep 60

done

echo ""
echo "=========================================="
echo "AUTONOMOUS LOOP COMPLETED"
echo "End: $(date)"
echo "Best log: /tmp/linux-boot-best.log"
echo "=========================================="
