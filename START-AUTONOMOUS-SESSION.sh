#!/bin/bash

# MASTER AUTONOMOUS SESSION CONTROLLER
# Main entry point for autonomous Linux boot mission

set -e

cd /home/ninadjangle/chipyard

# Configuration
DEADLINE_HOUR=8
ITERATION=0
MAX_ITERATIONS=20
SUCCESS=false

echo "=========================================="
echo "AUTONOMOUS LINUX BOOT MISSION"
echo "=========================================="
echo "Start: $(date)"
echo "Deadline: $(date -d "$(date +%Y-%m-%d) $DEADLINE_HOUR:00" +'%Y-%m-%d %H:%M:%S')"
echo "Max iterations: $MAX_ITERATIONS"
echo "=========================================="
echo ""

# Check deadline function
is_past_deadline() {
  CURRENT_HOUR=$(date +%H)
  if [ "$CURRENT_HOUR" -ge "$DEADLINE_HOUR" ]; then
    return 0
  fi
  return 1
}

# Get progress summary
get_progress() {
  BOOT_LOG="$1"
  if [ ! -f "$BOOT_LOG" ] || [ ! -s "$BOOT_LOG" ]; then
    echo "NO_OUTPUT"
    return
  fi

  if grep -q "# " "$BOOT_LOG" || grep -q "root@" "$BOOT_LOG"; then
    echo "SHELL_PROMPT"
    return
  fi

  if grep -q "UART0 is here" "$BOOT_LOG"; then
    AFTER=$(grep -A 20 "UART0 is here" "$BOOT_LOG" | wc -l)
    if [ "$AFTER" -lt 3 ]; then
      echo "UART_HANG"
    else
      echo "UART_PROGRESSING"
    fi
    return
  fi

  if grep -qi "panic\|oops\|error" "$BOOT_LOG"; then
    echo "KERNEL_ERROR"
    return
  fi

  echo "UNKNOWN"
}

# Main iteration loop
while [ $ITERATION -lt $MAX_ITERATIONS ]; do
  # Check deadline
  if is_past_deadline; then
    echo ""
    echo "⏰ Deadline reached (8 AM). Stopping."
    break
  fi

  ITERATION=$((ITERATION + 1))
  echo ""
  echo "╔════════════════════════════════════════╗"
  echo "║ ITERATION $ITERATION - $(date '+%H:%M:%S') ║"
  echo "╚════════════════════════════════════════╝"

  BOOT_LOG="/tmp/linux-iter-$ITERATION.log"

  echo "Step 1: Running boot test..."
  echo "  Output: $BOOT_LOG"
  echo "  Timeout: 35 minutes"

  # Run boot test in background
  timeout 2100 sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
    +permissive +max-cycles=20000000000 +permissive-off \
    software/firemarshal/images/firechip/br-base/br-base-bin \
    2>&1 > "$BOOT_LOG" &
  BOOT_PID=$!

  # Wait for output with timeout
  WAIT_TIME=0
  WAIT_TIMEOUT=2150
  OUTPUT_FOUND=false

  echo "Step 2: Monitoring for output..."

  while [ $WAIT_TIME -lt $WAIT_TIMEOUT ]; do
    if [ -f "$BOOT_LOG" ] && [ -s "$BOOT_LOG" ]; then
      echo "  ✓ Output detected at $(date '+%H:%M:%S')"
      OUTPUT_FOUND=true
      break
    fi

    if ! kill -0 $BOOT_PID 2>/dev/null; then
      echo "  ⚠ Simulator exited"
      break
    fi

    if [ $((WAIT_TIME % 300)) -eq 0 ] && [ $WAIT_TIME -gt 0 ]; then
      SIZE=$(wc -c < "$BOOT_LOG" 2>/dev/null || echo 0)
      echo "  [$(date +'%H:%M')] Waiting ($WAIT_TIME/$WAIT_TIMEOUT sec, output: $SIZE bytes)"
    fi

    sleep 5
    WAIT_TIME=$((WAIT_TIME + 5))
  done

  # Terminate boot test
  kill $BOOT_PID 2>/dev/null || true
  wait $BOOT_PID 2>/dev/null || true

  # Analyze results
  echo ""
  echo "Step 3: Analyzing output..."

  PROGRESS=$(get_progress "$BOOT_LOG")
  echo "  Status: $PROGRESS"

  # Show output sample
  if [ -f "$BOOT_LOG" ] && [ -s "$BOOT_LOG" ]; then
    SIZE=$(wc -c < "$BOOT_LOG")
    echo "  Log size: $SIZE bytes"
    echo ""
    echo "  First 30 lines of boot output:"
    head -30 "$BOOT_LOG" | sed 's/^/    /'
    echo ""
  fi

  # Check for success
  case "$PROGRESS" in
    SHELL_PROMPT)
      echo "╔════════════════════════════════════════╗"
      echo "║ ✓✓✓ MISSION ACCOMPLISHED ✓✓✓        ║"
      echo "║ LINUX FULLY BOOTED WITH SHELL          ║"
      echo "╚════════════════════════════════════════╝"
      echo ""
      tail -50 "$BOOT_LOG"
      SUCCESS=true
      break
      ;;
  esac

  # Save best log
  if [ $ITERATION -eq 1 ]; then
    cp "$BOOT_LOG" /tmp/linux-boot-best.log
  else
    BEST_SIZE=$(wc -c < /tmp/linux-boot-best.log 2>/dev/null || echo 0)
    CURR_SIZE=$(wc -c < "$BOOT_LOG" 2>/dev/null || echo 0)
    if [ "$CURR_SIZE" -gt "$BEST_SIZE" ]; then
      cp "$BOOT_LOG" /tmp/linux-boot-best.log
      echo "  (Saved as best log)"
    fi
  fi

  echo ""
  echo "Step 4: Preparing for next iteration..."
  echo "  Waiting 60 seconds..."
  sleep 60

done

# Summary
echo ""
echo "╔════════════════════════════════════════╗"
if [ "$SUCCESS" = true ]; then
  echo "║ ✓ SESSION SUCCESSFUL                  ║"
else
  echo "║ ⏰ SESSION TIMEOUT                      ║"
fi
echo "║ End: $(date '+%Y-%m-%d %H:%M:%S')        ║"
echo "║ Best log: /tmp/linux-boot-best.log     ║"
echo "╚════════════════════════════════════════╝"
