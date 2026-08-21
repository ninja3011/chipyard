#!/bin/bash

# FINAL MASTER WAIT SCRIPT
# Actively monitors boot output and triggers fixes automatically

set -e

cd /home/ninadjangle/chipyard

BOOT_LOG="/tmp/linux-boot-full.log"
ITERATION=0
DEADLINE_HOUR=8
SUCCESS=false

echo ""
echo "╔════════════════════════════════════════════════════════╗"
echo "║        MASTER AUTONOMOUS WAIT LOOP - STARTING           ║"
echo "║          Monitoring for boot output...                  ║"
echo "╚════════════════════════════════════════════════════════╝"
echo ""

# Main monitoring loop
while true; do
  # Check deadline (8 AM next day = 2026-08-20 08:00)
  CURRENT_TIME=$(date +%s)
  DEADLINE_TIME=$(date -d "2026-08-20 08:00:00" +%s 2>/dev/null || date +%s)  # Fallback to current time if date fails

  if [ "$CURRENT_TIME" -ge "$DEADLINE_TIME" ]; then
    echo "Deadline reached (8 AM on Aug 20)"
    break
  fi

  # Check for output
  if [ -f "$BOOT_LOG" ] && [ -s "$BOOT_LOG" ]; then
    SIZE=$(wc -c < "$BOOT_LOG")
    LINES=$(wc -l < "$BOOT_LOG")

    echo ""
    echo "╔════════════════════════════════════════════════════════╗"
    echo "║ ✓ BOOT OUTPUT DETECTED!                                ║"
    echo "║ Time: $(date '+%H:%M:%S')                                  ║"
    echo "║ Size: $SIZE bytes ($LINES lines)                         ║"
    echo "╚════════════════════════════════════════════════════════╝"
    echo ""

    # Display output
    echo "BOOT OUTPUT:"
    echo "═══════════════════════════════════════════════════════"
    cat "$BOOT_LOG"
    echo "═══════════════════════════════════════════════════════"
    echo ""

    # Auto-analyze and fix
    echo "Running auto-fix handler..."
    bash auto-fix-handler.sh "$BOOT_LOG"

    FIX_RESULT=$?

    if [ $FIX_RESULT -eq 0 ]; then
      echo ""
      echo "✓ MISSION ACCOMPLISHED"
      SUCCESS=true
      break
    else
      echo ""
      echo "Preparing for next iteration..."
      sleep 60
    fi
  fi

  # Non-blocking check
  sleep 5
done

echo ""
echo "╔════════════════════════════════════════════════════════╗"
if [ "$SUCCESS" = true ]; then
  echo "║ ✓ AUTONOMOUS MISSION SUCCESSFUL                       ║"
else
  echo "║ ⏰ AUTONOMOUS SESSION ENDED                            ║"
fi
echo "║ Time: $(date '+%Y-%m-%d %H:%M:%S')                      ║"
echo "╚════════════════════════════════════════════════════════╝"
