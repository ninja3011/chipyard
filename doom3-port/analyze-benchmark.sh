#!/bin/bash
# Automated Benchmark Analysis for DOOM on RISC-V
# Parses simulator output and calculates performance metrics

set -e

LOG_FILE="test-run.log"
METRICS_FILE="PERFORMANCE-METRICS.md"
REPORT_FILE="DAY4-BENCHMARK-RESULTS.md"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  DOOM on RISC-V - Benchmark Analysis (Day 4)              ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Check if log file exists and has data
if [ ! -f "$LOG_FILE" ]; then
    echo "❌ Error: $LOG_FILE not found"
    exit 1
fi

LOG_SIZE=$(wc -c < "$LOG_FILE")
if [ "$LOG_SIZE" -lt 100 ]; then
    echo "⏳ Log file still small ($LOG_SIZE bytes) - simulator may still be running"
    echo "   Waiting for completion..."
    exit 0
fi

echo "✓ Log file found ($LOG_SIZE bytes)"
echo ""

# Extract key metrics from log
echo "Analyzing performance data..."
echo ""

# Count frames
FRAMES=$(grep -o "\[FRAME [0-9]*\]" "$LOG_FILE" | tail -1 | grep -o "[0-9]*" | tail -1)
FRAMES=${FRAMES:-0}
echo "Frames Rendered: $FRAMES"

# Check for success
if grep -q "\[SUCCESS\]" "$LOG_FILE"; then
    STATUS="✅ SUCCESS"
elif grep -q "\[DOOM\]" "$LOG_FILE"; then
    STATUS="⏳ RUNNING / PARTIAL"
else
    STATUS="❌ FAILED"
fi
echo "Status: $STATUS"
echo ""

# Extract timing information (if available)
echo "Extracting timing data..."
CYCLES=$(grep -i "cycle" "$LOG_FILE" | head -1 || echo "")
if [ -n "$CYCLES" ]; then
    echo "  Found cycle data: $CYCLES"
fi

# Calculate derived metrics (if we have enough data)
if [ "$FRAMES" -gt 0 ]; then
    echo ""
    echo "Calculated Metrics:"
    echo "  Total Frames: $FRAMES"

    # Estimate based on available data
    echo ""
    echo "Performance Estimates (based on $FRAMES frames):"

    # Rough estimate: assume each frame ~30,000 cycles at 90 MHz
    EST_CYCLES=$((FRAMES * 28800))
    EST_SECONDS=$((EST_CYCLES / 90000000))

    if [ "$EST_SECONDS" -gt 0 ]; then
        EST_FPS=$((FRAMES / EST_SECONDS))
        echo "  Estimated FPS: $EST_FPS (based on $EST_SECONDS seconds)"
    fi
fi

echo ""
echo "✓ Analysis complete"
echo ""
echo "Next Steps:"
echo "  1. Review full log: cat $LOG_FILE | tail -50"
echo "  2. Extract detailed metrics manually if available"
echo "  3. Update PERFORMANCE-METRICS.md with actual data"
echo "  4. Compare to Day 3 estimates"
echo "  5. Document findings in DAY4-BENCHMARK-RESULTS.md"
echo ""
