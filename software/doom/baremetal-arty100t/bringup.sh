#!/bin/bash
# Ready-to-run Arty A7-100T bring-up script.
# Run this the moment the board is plugged in, programmed with
# Arty100THarness.bit (RocketArty100TVGAConfig -- see program_bitstream.tcl),
# and usbipd-attached into WSL.
# Usage: ./bringup.sh <doom|badapple> [/dev/ttyUSBx]
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UART_TSI="$HERE/../../../generators/testchipip/uart_tsi/uart_tsi"

PAYLOAD="${1:-}"
TTY="${2:-}"

case "$PAYLOAD" in
  doom)
    ELF="$HERE/doom-arty100t.elf"
    LOAD_MINUTES="5-6 minutes (30.7MB, dominated by the embedded 28.8MB Freedoom WAD)"
    READY_LINE="[doom] arty100t bare-metal backend up (VGA framebuffer @ 0x04000000)"
    ;;
  badapple)
    ELF="$HERE/badapple-arty100t.elf"
    LOAD_MINUTES="9-10 minutes (52.6MB, dominated by the embedded .vidf video)"
    READY_LINE="[badapple] loaded, playing"
    ;;
  *)
    echo "Usage: $0 <doom|badapple> [/dev/ttyUSBx]"
    exit 1
    ;;
esac

if [ -z "$TTY" ]; then
  echo "No TTY given. Devices currently present:"
  ls -la /dev/ttyUSB* 2>/dev/null || echo "  (none found -- has the board been usbipd-attached into WSL yet?"
  echo "   Run on Windows: usbipd list; usbipd bind --busid <ID>; usbipd attach --wsl --busid <ID>)"
  echo ""
  echo "Usage: $0 $PAYLOAD /dev/ttyUSBx"
  exit 1
fi

if [ ! -x "$UART_TSI" ]; then
  echo "uart_tsi not built at $UART_TSI -- run: cd generators/testchipip/uart_tsi && RISCV=<toolchain> make"
  exit 1
fi

if [ ! -f "$ELF" ]; then
  echo "$ELF not found -- run 'make' (doom) or 'make badapple' in this directory first."
  exit 1
fi

echo "=== Step 1: self-check the binary loads correctly (no boot yet) ==="
"$UART_TSI" +tty="$TTY" +selfcheck "$ELF"
echo "Self-check passed."
echo ""

echo "=== Step 2: real boot ($PAYLOAD) ==="
echo "Loading and running $(basename "$ELF") -- this takes roughly $LOAD_MINUTES"
echo "over UART-TSI at typical baud rates; this is expected, not a hang."
echo ""
echo "What 'it's working' looks like, in order:"
echo "  1. uart_tsi reports the binary loading"
echo "  2. Once running, the UART console should print:"
echo "       $READY_LINE"
echo "  3. On a real monitor connected to the VGA breakout on JA (pins"
echo "     G13=HSYNC, B11=VSYNC, A11=VIDEO -- see"
echo "     fpga/src/main/scala/arty100t/VGAHarnessBinder.scala), a"
echo "     monochrome 640x480 image should appear within a second or two"
echo "     of that console line printing."
if [ "$PAYLOAD" = "doom" ]; then
echo "  4. Try typing 'w', 'a', 's', 'd', space, enter into this terminal --"
echo "     these should move/turn/fire in-game if the console UART and the"
echo "     VGA output are both working."
fi
echo "  5. If the board resets or the console goes silent without the"
echo "     ready line ever printing, that's a real bug to chase -- not the"
echo "     expected slow-load behavior."
echo ""
echo "Starting..."
"$UART_TSI" +tty="$TTY" "$ELF"
