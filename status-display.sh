#!/bin/bash

# LIVE STATUS DISPLAY
# Shows autonomous session progress

cd /home/ninadjangle/chipyard

while true; do
  clear

  echo "╔════════════════════════════════════════════════════════╗"
  echo "║  AUTONOMOUS LINUX BOOT SESSION - LIVE STATUS           ║"
  echo "╚════════════════════════════════════════════════════════╝"
  echo ""

  # Time info
  CURRENT=$(date '+%H:%M:%S')
  DEADLINE="08:00:00"
  SECONDS_LEFT=$(( $(date -d "$(date +%Y-%m-%d) 08:00" +%s) - $(date +%s) ))
  HOURS_LEFT=$((SECONDS_LEFT / 3600))
  MINS_LEFT=$((( SECONDS_LEFT % 3600 ) / 60))

  echo "Current Time: $CURRENT"
  echo "Time Until Deadline: ${HOURS_LEFT}h ${MINS_LEFT}m"
  echo ""

  # Boot test progress
  echo "BOOT TEST STATUS:"
  if [ -f "/tmp/linux-boot-full.log" ]; then
    SIZE=$(wc -c < "/tmp/linux-boot-full.log" 2>/dev/null || echo 0)
    LINES=$(wc -l < "/tmp/linux-boot-full.log" 2>/dev/null || echo 0)
    if [ "$SIZE" -gt 0 ]; then
      echo "  Status: ✓ OUTPUT DETECTED"
      echo "  Size: $SIZE bytes ($LINES lines)"
    else
      ELAPSED=$(( $(date +%s) - $(stat -c %Y "/tmp/linux-boot-full.log" 2>/dev/null) ))
      echo "  Status: Running..."
      echo "  Elapsed: $(($ELAPSED / 60))min $(($ELAPSED % 60))sec"
    fi
  else
    echo "  Status: Starting..."
  fi
  echo ""

  # Simulator check
  echo "SIMULATOR:"
  SIM_COUNT=$(ps aux | grep "MediumBoomV3Config" | grep -v grep | wc -l)
  if [ "$SIM_COUNT" -gt 0 ]; then
    echo "  Status: ✓ Running ($SIM_COUNT processes)"
  else
    echo "  Status: Not running"
  fi
  echo ""

  # File status
  echo "LOG FILES:"
  if [ -f "/tmp/linux-boot-best.log" ]; then
    BEST_SIZE=$(wc -c < "/tmp/linux-boot-best.log")
    echo "  Best: $BEST_SIZE bytes"
  fi
  if [ -f "/tmp/linux-boot-full.log" ]; then
    FULL_SIZE=$(wc -c < "/tmp/linux-boot-full.log")
    echo "  Current: $FULL_SIZE bytes"
  fi
  echo ""

  # Quick reference
  echo "QUICK COMMANDS:"
  echo "  View boot output: tail -20 /tmp/linux-boot-full.log"
  echo "  Apply device tree fix: bash quick-fix-uart-hang.sh"
  echo "  Check simulator: ps aux | grep MediumBoomV3Config"
  echo ""

  echo "═══════════════════════════════════════════════════════"
  echo "Refresh: Every 10 seconds | Press Ctrl+C to stop"
  echo "═══════════════════════════════════════════════════════"

  sleep 10
done
