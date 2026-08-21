#!/bin/bash

# Wait for boot test to complete and analyze output

cd /home/ninadjangle/chipyard

BOOT_LOG="/tmp/linux-boot-full.log"
TIMEOUT=2100  # 35 minutes (little more than the 2000s test timeout)
START_TIME=$(date +%s)

echo "Waiting for boot test output..."
echo "Log file: $BOOT_LOG"
echo "Timeout: $TIMEOUT seconds"
echo ""

while true; do
  ELAPSED=$(($(date +%s) - START_TIME))

  if [ -f "$BOOT_LOG" ] && [ -s "$BOOT_LOG" ]; then
    SIZE=$(wc -c < "$BOOT_LOG")
    echo "[$(date +'%H:%M:%S')] Output detected! ($SIZE bytes)"
    echo ""
    echo "=== BOOT OUTPUT ==="
    cat "$BOOT_LOG"
    echo ""
    echo "=== END OUTPUT ==="
    break
  fi

  if [ $ELAPSED -ge $TIMEOUT ]; then
    echo "[$(date +'%H:%M:%S')] Timeout waiting for output"
    echo "Test may have completed silently (no output)"
    break
  fi

  # Check if simulator still running
  if ! pgrep -f "MediumBoomV3Config.*br-base-bin" > /dev/null; then
    echo "[$(date +'%H:%M:%S')] Simulator exited"
    if [ -f "$BOOT_LOG" ]; then
      echo "Output file contents:"
      cat "$BOOT_LOG"
    else
      echo "No output file generated"
    fi
    break
  fi

  # Show progress every 60 seconds
  if [ $((ELAPSED % 60)) -eq 0 ]; then
    echo "[$(date +'%H:%M:%S')] Still waiting... ($ELAPSED / $TIMEOUT seconds)"
  fi

  sleep 5
done

echo ""
echo "Test phase complete. Ready for analysis/fix application."
