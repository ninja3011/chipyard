#!/bin/bash

# WATCH FOR BOOT OUTPUT
# Continuously checks and alerts when output appears

BOOT_LOG="/tmp/linux-boot-full.log"
LAST_SIZE=0

echo "Watching for boot output..."
echo "Log: $BOOT_LOG"
echo ""

while true; do
  if [ -f "$BOOT_LOG" ]; then
    CURRENT_SIZE=$(wc -c < "$BOOT_LOG")

    if [ "$CURRENT_SIZE" -gt 0 ] && [ "$LAST_SIZE" -eq 0 ]; then
      echo ""
      echo "╔════════════════════════════════════════╗"
      echo "║ ✓ BOOT OUTPUT DETECTED!                ║"
      echo "║ Time: $(date '+%H:%M:%S')                    ║"
      echo "║ Size: $CURRENT_SIZE bytes                    ║"
      echo "╚════════════════════════════════════════╝"
      echo ""
      echo "Content:"
      cat "$BOOT_LOG"
      echo ""
      echo "Ready for analysis and fix selection"
      break
    fi

    LAST_SIZE=$CURRENT_SIZE
  fi

  sleep 5
done
