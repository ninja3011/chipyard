#!/bin/bash
# Analyze device tree mismatch between kernel and BOOM test harness

cd /home/ninadjangle/chipyard

echo "=== Device Tree Analysis ==="
echo ""

echo "1. Kernel expects (from strings):"
strings software/firemarshal/boards/default/linux/vmlinux | grep -E "compatible|uart|timer|cpu|memory" | head -20

echo ""
echo "2. BOOM test harness provides (from Scala config):"
grep -r "PeripheryUART\|UART\|Timer" generators/boom/src/main/scala/ 2>/dev/null | head -10 || echo "(checking RTL...)"

echo ""
echo "3. Test harness device tree location:"
find . -name "*.dts" -o -name "*.dtsi" 2>/dev/null | grep -i test | head -5

echo ""
echo "4. Checking if device tree is embedded in br-base-bin:"
strings software/firemarshal/images/firechip/br-base/br-base-bin | grep -E "compatible|model" | head -10

echo ""
echo "═════════════════════════════════════════════════════════════"
echo "ACTION: If no device tree in kernel, create one for BOOM test harness"
