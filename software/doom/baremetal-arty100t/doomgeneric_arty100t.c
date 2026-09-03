// doomgeneric backend for bare-metal RocketArty100TConfig.
//
// Video: DG_DrawFrame downsamples DG_ScreenBuffer (640x400 BGRA,
// doomgeneric's default resolution) into the real 8-bit color (3-3-2 RGB)
// VGA framebuffer added by chipyard.vga.TLVGAFramebuffer
// (RocketArty100TVGAConfig only -- the plain RocketArty100TConfig build
// has no such peripheral and must not be linked against this build
// variant). The peripheral is 320x240, 1 byte/pixel, bits
// [7:5]=R[2:0],[4:2]=G[2:0],[1:0]=B[1:0] (see VGAFramebuffer.scala's
// header comment for why this is 8 bits/pixel and not the 16-bit design
// tried first -- a real Arty A7-100T Block RAM budget constraint, not a
// software choice); DOOM's 640x400 is nearest-neighbor downsampled 2x to
// 320x200 and vertically centered (20-row letterbox top and bottom)
// rather than stretched, to avoid distortion. Each source pixel's 8-bit
// B/G/R channels are downsampled to 3/3/2 bits by taking the top bits --
// no thresholding, this is real (if lower-precision) color.
//
// Input: DG_GetKey polls the same UART console already wired up in
// RocketArty100TConfig (confirmed: serial@0x10020000, sifive,uart0, 50MHz
// pbus clock) for single-byte keypresses sent from a terminal on the host.
// This deliberately reuses the UART bandwidth math from
// ARTY-A7-100T-PROJECT-SCOPE.md: a keypress is 1 byte, so unlike video,
// this needs no new hardware or pin binding at all.
//
// Mapping is simple WASD + space/enter/escape, not arrow keys -- a raw
// single-byte serial link is a bad fit for parsing ANSI arrow-key escape
// sequences, and WASD needs no escape-sequence parsing at all.

#include "doomgeneric.h"
#include "doomkeys.h"
#include "uart.h"
#include <stdint.h>

// Real address: chosen directly in the Chisel config
// (RocketArty100TVGAConfig's WithVGAFramebuffer(address = 0x4000000L)),
// not discovered after the fact -- we control it, so it's not a guess.
#define FRAMEBUFFER_BASE 0x04000000UL
#define FB_WIDTH 320
#define FB_HEIGHT 240
#define FB_BYTES_PER_PIXEL 1
#define FB_BYTES_PER_ROW (FB_WIDTH * FB_BYTES_PER_PIXEL)
#define FB_ROW_OFFSET_Y ((FB_HEIGHT - DOOMGENERIC_RESY / 2) / 2) /* letterbox */

// Real 3-3-2-bit-per-channel color sample, no thresholding. DG_ScreenBuffer
// is BGRA (doomgeneric's convention on every backend in this project so
// far): byte 0 = B, byte 1 = G, byte 2 = R. Downsample each 8-bit channel
// by keeping its top 3 (R,G) or 2 (B) bits, packed [7:5]=R,[4:2]=G,[1:0]=B
// -- matches VGAFramebuffer.scala's read-side extraction exactly.
static inline uint8_t samplePixelColor(int srcX, int srcY) {
  uint32_t px = DG_ScreenBuffer[srcY * DOOMGENERIC_RESX + srcX];
  uint8_t b2 = (uint8_t)((px >> 6) & 0x3);
  uint8_t g3 = (uint8_t)((px >> 13) & 0x7);
  uint8_t r3 = (uint8_t)((px >> 21) & 0x7);
  return (uint8_t)((r3 << 5) | (g3 << 2) | b2);
}

// mtime tick rate: confirmed (not assumed) from this exact config's own
// generated DTS -- `timebase-frequency = <50000>` under /cpus, in
// fpga/generated-src/.../*.dts, generated 2026-08-28. This is NOT the
// common 1MHz convention some rocket-chip configs use; RocketArty100TConfig
// really does run its timebase at 50kHz. Getting this wrong would have
// made DG_SleepMs and DG_GetTicksMs off by 20x (a "1MHz assumed, 50kHz
// real" mismatch makes every sleep 20x longer than intended), so this is
// worth having caught before real hardware time, not during it.
#define MTIME_HZ 50000ULL

static uint64_t rdtime(void) {
  uint64_t t;
  __asm__ volatile ("rdtime %0" : "=r"(t));
  return t;
}

static uint64_t s_startTimeTicks;

// Key state machine: we get raw bytes one at a time from the UART with no
// separate press/release signal (a real keyboard scancode has both; a dumb
// serial link only has "a byte arrived"). We synthesize a release shortly
// after each press so DOOM's input model (which expects press/release
// pairs) still works, at the cost of not supporting "held key" repeat the
// way a real keyboard would. Good enough for a first playable cut; a
// real solution would need a richer protocol from the host terminal.
#define KEYQUEUE_SIZE 16
static unsigned short s_keyQueue[KEYQUEUE_SIZE]; // (pressed<<8)|doomkey
static unsigned int s_keyQueueHead = 0;
static unsigned int s_keyQueueTail = 0;

static void pushKeyEvent(int pressed, unsigned char doomkey) {
  unsigned int next = (s_keyQueueTail + 1) % KEYQUEUE_SIZE;
  if (next == s_keyQueueHead) return; // queue full, drop
  s_keyQueue[s_keyQueueTail] = (unsigned short)(((pressed ? 1 : 0) << 8) | doomkey);
  s_keyQueueTail = next;
}

static unsigned char mapByteToDoomKey(unsigned char c) {
  switch (c) {
    case 'w': case 'W': return KEY_UPARROW;
    case 's': case 'S': return KEY_DOWNARROW;
    case 'a': case 'A': return KEY_LEFTARROW;
    case 'd': case 'D': return KEY_RIGHTARROW;
    case ' ':           return KEY_FIRE;
    case 'e': case 'E': return KEY_USE;
    case 13: case 10:   return KEY_ENTER;
    case 27:             return KEY_ESCAPE;
    default:             return 0;
  }
}

void DG_Init(void) {
  uart_init();
  s_startTimeTicks = rdtime();
  uart_puts("\r\n[doom] arty100t bare-metal backend up (VGA framebuffer @ 0x04000000)\r\n");
}

void DG_DrawFrame(void) {
  // 1-byte-per-pixel color memory, [7:5]=R[2:0],[4:2]=G[2:0],[1:0]=B[1:0]
  // -- matches VGAFramebuffer.scala's read-side extraction exactly, same
  // encoding the real captured-frame verification testbench was checked
  // against.
  volatile uint8_t *fb = (volatile uint8_t *)FRAMEBUFFER_BASE;

  for (int y = 0; y < FB_HEIGHT; y++) {
    int srcY2 = y - FB_ROW_OFFSET_Y; // row within the centered 200-row image
    for (int x = 0; x < FB_WIDTH; x++) {
      uint8_t pixel = 0;
      if (srcY2 >= 0 && srcY2 < DOOMGENERIC_RESY / 2) {
        pixel = samplePixelColor(x * 2, srcY2 * 2);
      }
      fb[y * FB_BYTES_PER_ROW + x * FB_BYTES_PER_PIXEL] = pixel;
    }
  }
}

void DG_SleepMs(uint32_t ms) {
  uint64_t target = rdtime() + ((uint64_t)ms * MTIME_HZ) / 1000ULL;
  while (rdtime() < target) { }
}

uint32_t DG_GetTicksMs(void) {
  uint64_t elapsed = rdtime() - s_startTimeTicks;
  return (uint32_t)((elapsed * 1000ULL) / MTIME_HZ);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
  // Drain any new raw bytes into press+release event pairs first.
  int c;
  while ((c = uart_getc_nonblock()) >= 0) {
    unsigned char dk = mapByteToDoomKey((unsigned char)c);
    if (dk == 0) continue;
    pushKeyEvent(1, dk);
    pushKeyEvent(0, dk);
  }

  if (s_keyQueueHead == s_keyQueueTail) return 0;
  unsigned short ev = s_keyQueue[s_keyQueueHead];
  s_keyQueueHead = (s_keyQueueHead + 1) % KEYQUEUE_SIZE;
  *pressed = (ev >> 8) & 1;
  *doomKey = (unsigned char)(ev & 0xFF);
  return 1;
}

void DG_SetWindowTitle(const char *title) {
  (void)title; // no window, nothing to do
}

int main(int argc, char **argv) {
  // Bare-metal newlib crt0 has no real argv to hand us -- don't trust
  // whatever raw values it passes (nothing in the engine dereferences
  // myargv[0] today, but that's fragile to depend on). Pass a safe,
  // always-valid fake argv instead.
  (void)argc; (void)argv;
  static char *fake_argv[] = { "doom", NULL };
  doomgeneric_Create(1, fake_argv);
  while (1) {
    doomgeneric_Tick();
  }
  return 0;
}
