#!/bin/bash
# Ready-to-run Arty A7-100T bring-up script.
# Run this the moment the board is plugged in and the bitstream is programmed.
# Usage: ./bringup.sh [/dev/ttyUSBx]
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UART_TSI="$HERE/../../../generators/testchipip/uart_tsi/uart_tsi"
ELF="$HERE/doom-arty100t.elf"
TTY="${1:-}"

if [ -z "$TTY" ]; then
  echo "No TTY given. Devices currently present:"
  ls -la /dev/ttyUSB* 2>/dev/null || echo "  (none found -- has the board been usbipd-attached into WSL yet?"
  echo "   Run on Windows: usbipd list; usbipd bind --busid <ID>; usbipd attach --wsl --busid <ID>)"
  echo ""
  echo "Usage: $0 /dev/ttyUSBx"
  exit 1
fi

if [ ! -x "$UART_TSI" ]; then
  echo "uart_tsi not built at $UART_TSI -- run: cd generators/testchipip/uart_tsi && RISCV=<toolchain> make"
  exit 1
fi

if [ ! -f "$ELF" ]; then
  echo "$ELF not found -- run 'make' in this directory first."
  exit 1
fi

echo "=== Step 1: self-check the binary loads correctly (no boot yet) ==="
"$UART_TSI" +tty="$TTY" +selfcheck "$ELF"
echo "Self-check passed."
echo ""

echo "=== Step 2: real boot ==="
echo "Loading and running doom-arty100t.elf (30.7MB -- this WAD-embedded"
echo "binary takes roughly 5-6 minutes to transfer over UART-TSI at typical"
echo "baud rates; this is expected, not a hang -- see"
echo "ARTY-A7-100T-BRINGUP-PREP-REPORT.md section 6)."
echo ""
echo "What 'it's working' looks like, in order:"
echo "  1. uart_tsi reports the binary loading (progress, if it prints any)"
echo "  2. Once running, the UART console should print:"
echo "       [doom] arty100t bare-metal backend up"
echo "     immediately followed by (since no VGA peripheral exists yet):"
echo "       [doom] WARNING: no framebuffer peripheral yet -- rendering is happening but not visible"
echo "  3. Try typing 'w', 'a', 's', 'd', space, enter into this terminal --"
echo "     if the UART console is working, keypresses are being read (no"
echo "     visible confirmation without a display, but no crash either)."
echo "  4. If the board resets or the console goes silent without the"
echo "     backend-up message ever printing, that's a real bug to chase --"
echo "     not the expected slow-WAD-load behavior."
echo ""
echo "Starting..."
"$UART_TSI" +tty="$TTY" "$ELF"
