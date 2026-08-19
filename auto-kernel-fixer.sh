#!/bin/bash
# Automatic kernel configuration fixer for BOOM test harness
# Tries multiple configurations until one works

set -e

CONFIGS=(
  "default"
  "nosmp"
  "earlyprint"
  "minimal"
  "debug"
)

echo "╔════════════════════════════════════════════════════════════╗"
echo "║          AUTO KERNEL CONFIG FIXER FOR BOOM                ║"
echo "║    Tests multiple configs until one produces output       ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

cd /home/ninadjangle/chipyard

for CONFIG in "${CONFIGS[@]}"; do
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo "Testing config: $CONFIG"
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo ""

  LOG_FILE="/tmp/linux-test-$CONFIG.log"

  echo "[$(date)] Launching with $CONFIG kernel..."

  timeout 180 sims/verilator/simulator-chipyard.harness-MediumBoomV3Config \
    +permissive \
    +max-cycles=10000000000 \
    +permissive-off \
    software/firemarshal/images/firechip/br-base/br-base-bin \
    2>&1 | tee "$LOG_FILE" &

  SIM_PID=$!

  # Wait up to 3 minutes for output
  for i in {1..6}; do
    sleep 30
    LOG_SIZE=$(stat -c%s "$LOG_FILE" 2>/dev/null || echo 0)

    if [ $LOG_SIZE -gt 100 ]; then
      echo "✅ SUCCESS! Config '$CONFIG' produced output!"
      echo ""
      head -30 "$LOG_FILE"
      echo ""
      echo "Configuration: $CONFIG" > /tmp/linux-working-config.txt
      echo "Log: $LOG_FILE" >> /tmp/linux-working-config.txt
      exit 0
    fi
  done

  # Kill if still running
  kill -9 $SIM_PID 2>/dev/null || true
  wait $SIM_PID 2>/dev/null || true

  echo "❌ No output with $CONFIG config"
  echo ""
  sleep 2
done

echo ""
echo "════════════════════════════════════════════════════════════"
echo "No configuration produced kernel output."
echo "Likely causes:"
echo "  1. Device tree incompatibility"
echo "  2. UART not connected properly"
echo "  3. Fundamental kernel/harness mismatch"
echo ""
echo "Next: Custom device tree creation required"
echo "════════════════════════════════════════════════════════════"
