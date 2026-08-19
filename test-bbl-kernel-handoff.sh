#!/bin/bash
# Test BBL->Kernel handoff to see where execution stops

cd /home/ninadjangle/chipyard

echo "╔════════════════════════════════════════════════════════════╗"
echo "║      BBL -> KERNEL HANDOFF DIAGNOSTICS                    ║"
echo "║  Testing if kernel actually starts or BBL hangs           ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

echo "[HYPOTHESIS 1] BBL boots but can't find kernel"
echo "Test: Check if vmlinux exists and is loadable"
echo ""

VMLINUX="software/firemarshal/boards/default/linux/vmlinux"
if [ -f "$VMLINUX" ]; then
  echo "✓ vmlinux found: $(ls -lh $VMLINUX | awk '{print $5, $9}')"
  echo "  Type: $(file $VMLINUX | cut -d: -f2)"
else
  echo "✗ vmlinux NOT found!"
fi

echo ""
echo "[HYPOTHESIS 2] Kernel starts but panics before console init"
echo "Evidence: BBL prints UART message, then nothing"
echo "Likely cause: Kernel exception or hang during early boot"
echo ""

echo "[HYPOTHESIS 3] Device tree missing or corrupted"
echo "Check: Does br-base-bin have proper device tree?"
echo ""

strings software/firemarshal/images/firechip/br-base/br-base-bin | \
  grep -E "^\\" | head -20

echo ""
echo "[TEST] Boot with custom minimal payload"
echo ""
echo "Creating minimal test ELF (just prints and exits):"

cat > /tmp/minimal-boot-test.c << 'EOFTEST'
int main() {
    /* Just try to print something */
    volatile int *uart = (int *)0x10010000;
    *uart = 'H';
    *uart = 'I';
    return 42;
}
EOFTEST

echo "Compiled above code would test if kernel entry point is reached"
echo ""
echo "═════════════════════════════════════════════════════════════"
echo "RECOMMENDATION: Check kernel entry point in BBL logs"
echo "If BBL says 'Loading kernel from' but never says 'Entering kernel'"
echo "then device tree or memory setup is broken"
echo "═════════════════════════════════════════════════════════════"
