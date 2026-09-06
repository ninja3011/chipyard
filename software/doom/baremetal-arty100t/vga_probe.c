/*
 * VGA-only integration test -- deliberately has ZERO dependency on the
 * UART peripheral or any other subsystem. If this test's result is
 * ambiguous while sharing code with UART, a hang in one can mask the
 * other; keeping them fully independent means a "no signal" or "black
 * screen" result here can only be about the VGA path (Pmod seating,
 * cable, converter, monitor, or the VGA peripheral/RTL itself) and
 * nothing else.
 *
 * What to expect on a working link: the whole screen cycles through
 * solid red -> green -> blue -> white, roughly once per second, forever.
 * Any of these outcomes is useful diagnostic information:
 *   - Monitor shows "no signal"      -> sync (HSYNC/VSYNC) isn't reaching
 *                                        it: Pmod seating, cable, or a
 *                                        real RTL/timing bug.
 *   - Monitor locks on but stays     -> signal timing is fine, but either
 *     black and never changes           the CPU isn't executing, or the
 *                                        framebuffer write path itself
 *                                        has a bug (bad base address,
 *                                        wrong color encoding, etc).
 *   - Colors cycle correctly         -> the full path (CPU -> framebuffer
 *                                        write -> VGA peripheral -> Pmod
 *                                        -> cable -> converter -> monitor)
 *                                        works end to end.
 */
#include <stdint.h>

#define FRAMEBUFFER_BASE 0x04000000UL
#define FB_WIDTH 320
#define FB_HEIGHT 240

/* 8-bit color, [7:5]=R[2:0], [4:2]=G[2:0], [1:0]=B[1:0] -- matches
 * VGAFramebuffer.scala's read-side extraction exactly. */
#define COLOR_RED   0xE0
#define COLOR_GREEN 0x1C
#define COLOR_BLUE  0x03
#define COLOR_WHITE 0xFF

int main(void) {
  volatile uint8_t *fb = (volatile uint8_t *)FRAMEBUFFER_BASE;
  const uint8_t colors[4] = { COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_WHITE };
  int c = 0;

  while (1) {
    uint8_t color = colors[c % 4];
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
      fb[i] = color;
    }
    c++;
    for (volatile long i = 0; i < 20000000; i++) { }
  }
  return 0;
}
