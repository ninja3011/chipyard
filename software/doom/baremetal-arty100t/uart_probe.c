/*
 * FT232RL / console-UART-only integration test -- deliberately has ZERO
 * dependency on VGA or any other subsystem, and tests BOTH directions of
 * the link (not just print output), since a link that can send but not
 * receive (or vice versa) is a different bug than total silence.
 *
 * How to use: load this, then watch the terminal connected to the
 * FT232RL (e.g. `cat /dev/ttyUSB0` after `stty -F /dev/ttyUSB0 115200
 * raw -echo`). Then type characters into that same terminal.
 *
 * What to expect on a working link:
 *   1. "UART PROBE: link up..." prints once, immediately after boot.
 *   2. Every character you type gets echoed back within a fraction of a
 *      second.
 *
 * Diagnostic outcomes:
 *   - Nothing ever prints, even the boot message -> either the TX
 *     direction is broken (wiring, peripheral, or the CPU never reached
 *     this code at all -- cross-check with vga_probe.c, which shares no
 *     code with this file, to see if the CPU is executing anything).
 *   - Boot message prints, but typed characters never echo -> TX works,
 *     RX direction specifically is broken (check the RX wire/pin
 *     separately from TX -- they are not symmetric, a working transmit
 *     path says nothing about the receive path).
 *   - Both directions work -> the full console link is proven good.
 */
#include "uart.h"

int main(void) {
  uart_init();
  uart_puts("\r\nUART PROBE: link up. Type characters -- they should echo back.\r\n");

  while (1) {
    int c = uart_getc_nonblock();
    if (c >= 0) {
      uart_putc((char)c);
      if (c == '\r') {
        uart_putc('\n');
      }
    }
  }
  return 0;
}
