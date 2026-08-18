#!/bin/bash
# Performance Testing Script for DOOM on RISC-V
# Boots Linux on BOOM simulator, runs DOOM binary, collects metrics

set -e

# Configuration
SIMULATOR="/home/ninadjangle/chipyard/sims/verilator/simulator-chipyard.harness-MediumBoomV3Config"
LINUX_IMAGE="/home/ninadjangle/chipyard/software/firemarshal/images/firechip/br-base/br-base-bin"
DOOM_BINARY="/home/ninadjangle/chipyard/doom3-port/output/doom.riscv"
TEST_DIR="/home/ninadjangle/chipyard/doom3-port"
LOG_FILE="$TEST_DIR/test-run.log"
METRICS_FILE="$TEST_DIR/PERFORMANCE-METRICS.md"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  DOOM on RISC-V - Performance Test & Metrics Collection    ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Verify files exist
echo -e "${YELLOW}[SETUP] Verifying test files...${NC}"
if [ ! -f "$SIMULATOR" ]; then
    echo -e "${RED}❌ Simulator not found: $SIMULATOR${NC}"
    exit 1
fi
if [ ! -f "$LINUX_IMAGE" ]; then
    echo -e "${RED}❌ Linux image not found: $LINUX_IMAGE${NC}"
    exit 1
fi
if [ ! -f "$DOOM_BINARY" ]; then
    echo -e "${RED}❌ DOOM binary not found: $DOOM_BINARY${NC}"
    exit 1
fi
echo -e "${GREEN}✓ All files verified${NC}"
echo ""

# Display test configuration
echo -e "${BLUE}[CONFIG] Test Configuration:${NC}"
echo "  Simulator:     $(basename $SIMULATOR)"
echo "  Linux Image:   $(basename $LINUX_IMAGE)"
echo "  DOOM Binary:   $(basename $DOOM_BINARY) ($(stat -f%z \"$DOOM_BINARY\" 2>/dev/null || stat -c%s \"$DOOM_BINARY\") bytes)"
echo "  Output Log:    $LOG_FILE"
echo "  Metrics File:  $METRICS_FILE"
echo ""

# Show expected performance targets
echo -e "${BLUE}[TARGETS] Expected Performance Goals:${NC}"
echo "  Target FPS:              35+"
echo "  Target IPC:              2.5+"
echo "  Target L1-D Hit Rate:    80%+"
echo "  Target L2 Hit Rate:      70%+"
echo "  Target Branch Accuracy:  85%+"
echo ""

# Instructions for test
echo -e "${YELLOW}[INFO] Simulator will boot Linux (5-15 minutes in RTL simulation)${NC}"
echo -e "${YELLOW}[INFO] After boot, a shell prompt will appear.${NC}"
echo -e "${YELLOW}[INFO] Commands to run DOOM:${NC}"
echo "       # Copy binary to RAM disk"
echo "       cp /doom.riscv /tmp/"
echo "       chmod +x /tmp/doom.riscv"
echo ""
echo "       # Run the binary"
echo "       /tmp/doom.riscv"
echo ""
echo "       # Exit simulator after test"
echo "       exit"
echo ""

# Create a script to run inside Linux
LINUX_COMMANDS=$(cat <<'EOF'
#!/bin/sh
# Commands to run inside Linux during test

# Copy and run DOOM
echo "[DOOM] Copying binary to RAM disk..."
cp /doom.riscv /tmp/ 2>/dev/null || echo "[DOOM] Copy failed, binary in /"

echo "[DOOM] Setting executable..."
chmod +x /tmp/doom.riscv 2>/dev/null || chmod +x /doom.riscv

echo "[DOOM] Starting DOOM test..."
echo "[DOOM] =================================================="

# Run DOOM and capture output
if [ -f /tmp/doom.riscv ]; then
    /tmp/doom.riscv
else
    /doom.riscv
fi

echo "[DOOM] =================================================="
echo "[DOOM] Test completed"

# Attempt to extract simulator statistics if available
if [ -f /proc/stat ]; then
    echo "[METRICS] System Statistics:"
    cat /proc/stat | head -3
fi

# Exit
sleep 2
exit 0
EOF
)

# Start the simulator
echo -e "${BLUE}[START] Launching simulator...${NC}"
echo "Starting simulator at $(date)" > "$LOG_FILE"
echo "" >> "$LOG_FILE"

# Run simulator with timeout (20 minutes)
# Note: We'll capture stdout/stderr to analyze performance data
timeout 1200 "$SIMULATOR" "$LINUX_IMAGE" 2>&1 | tee -a "$LOG_FILE" &
SIMULATOR_PID=$!

# Wait for simulator to complete or timeout
wait $SIMULATOR_PID || WAIT_STATUS=$?

echo ""
echo -e "${BLUE}[COMPLETE] Test execution finished${NC}"
echo ""

# Analyze the log file
echo -e "${BLUE}[ANALYSIS] Processing results...${NC}"

# Extract key metrics from log
echo "Analyzing simulator output..."

# Count frames
FRAME_COUNT=$(grep -c "\[FRAME" "$LOG_FILE" 2>/dev/null || echo "0")
echo -e "${GREEN}✓ Frames rendered: $FRAME_COUNT${NC}"

# Check for success markers
if grep -q "\[SUCCESS\]" "$LOG_FILE" 2>/dev/null; then
    echo -e "${GREEN}✓ Test completed successfully${NC}"
elif grep -q "Test completed" "$LOG_FILE" 2>/dev/null; then
    echo -e "${GREEN}✓ Test completed (check log for details)${NC}"
else
    echo -e "${YELLOW}⚠ Test status unclear (see log for details)${NC}"
fi

# Try to extract cycle count from log
CYCLES=$(grep -i "cycles" "$LOG_FILE" | tail -1 || echo "Unable to extract")
echo -e "${YELLOW}Cycles: $CYCLES${NC}"

echo ""
echo -e "${BLUE}[SUMMARY]${NC}"
echo "  Log file:     $LOG_FILE"
echo "  Frames:       $FRAME_COUNT"
echo "  Status:       Check log file for detailed output"
echo ""

# Display last 30 lines of log
echo -e "${BLUE}[LOG TAIL] Last 30 lines of simulator output:${NC}"
tail -30 "$LOG_FILE"

echo ""
echo -e "${BLUE}[NEXT STEPS]${NC}"
echo "1. Review full log:        cat $LOG_FILE"
echo "2. Update metrics:         Edit $METRICS_FILE"
echo "3. Commit results:         git add -A && git commit -m 'Day 3: Performance metrics collected'"
echo ""

# Create summary report
echo -e "${GREEN}Test run complete!${NC}"
echo "Output saved to: $LOG_FILE"
echo ""
