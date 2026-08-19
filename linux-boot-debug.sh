#!/bin/bash
# Linux boot debugging harness - automated iteration
# Goal: Get kernel output, identify hang point, fix

set -e

cd /home/ninadjangle/chipyard
source .conda-env/bin/activate

echo "╔════════════════════════════════════════════════════════════╗"
echo "║        BOOM LINUX BOOT DEBUG HARNESS v1                   ║"
echo "║  Objective: Trace kernel boot to identify hang point      ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Configuration
MAX_WAIT=180  # 3 minutes max per attempt
CYCLE_LIMIT=10000000000  # 10 billion cycles (should be enough)
TIMEOUT_TOTAL=600  # 10 minute wall clock timeout

# Test 1: Try with higher cycle limit + HTIF debug
echo "[TEST 1] Boot with max cycle budget - trace where kernel goes"
echo "Time limit: ${MAX_WAIT}s | Cycles: ${CYCLE_LIMIT}"
echo "Logging to: /tmp/linux-debug-1.log"
echo ""

timeout $MAX_WAIT sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
  +permissive \
  +max-cycles=$CYCLE_LIMIT \
  +permissive-off \
  software/firemarshal/images/firechip/br-base/br-base-bin \
  2>&1 | tee /tmp/linux-debug-1.log &

SIM_PID=$!
echo "Simulator PID: $SIM_PID"
echo ""

# Monitor boot progress
for i in {1..6}; do
  sleep 30
  ELAPSED=$((i * 30))
  LOG_SIZE=$(stat -c%s /tmp/linux-debug-1.log 2>/dev/null || echo 0)

  echo "[${ELAPSED}s] Log size: $LOG_SIZE bytes"

  if [ $LOG_SIZE -gt 100 ]; then
    echo "✅ KERNEL OUTPUT DETECTED!"
    echo ""
    head -50 /tmp/linux-debug-1.log
    break
  fi

  if ! ps -p $SIM_PID >/dev/null 2>&1; then
    echo "⚠️  Simulator exited"
    break
  fi
done

wait $SIM_PID 2>/dev/null || true

echo ""
echo "═════════════════════════════════════════════════════════════"
echo "RESULTS:"
echo "─────────────────────────────────────────────────────────────"
echo "Log size: $(stat -c%s /tmp/linux-debug-1.log 2>/dev/null || echo 0) bytes"
if [ -s /tmp/linux-debug-1.log ]; then
  echo ""
  echo "Output:"
  cat /tmp/linux-debug-1.log
else
  echo "⚠️  No kernel output (kernel hanging during init)"
  echo ""
  echo "Likely causes:"
  echo "  1. Device tree mismatch (kernel looking for devices not in harness)"
  echo "  2. UART initialization failing"
  echo "  3. Memory initialization hang"
  echo "  4. Kernel panic before console output"
fi
echo ""
echo "═════════════════════════════════════════════════════════════"
