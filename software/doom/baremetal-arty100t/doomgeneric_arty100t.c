// doomgeneric backend for bare-metal RocketArty100TConfig.
//
// Video: DG_DrawFrame downsamples+thresholds DG_ScreenBuffer (640x400 RGBA,
// doomgeneric's default resolution) into the real monochrome VGA
// framebuffer added by chipyard.vga.TLVGAFramebuffer (RocketArty100TVGAConfig
// only -- the plain RocketArty100TConfig build has no such peripheral and
// must not be linked against this build variant). The peripheral is
// 320x240, 1 bit/pixel, packed 32 pixels/word; DOOM's 640x400 is nearest-
// neighbor downsampled 2x to 320x200 and vertically centered (20-row
// letterbox top and bottom) rather than stretched, to avoid distortion.
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
#define FB_BYTES_PER_ROW (FB_WIDTH / 8)
#define FB_ROW_OFFSET_Y ((FB_HEIGHT - DOOMGENERIC_RESY / 2) / 2) /* letterbox */

static inline int sampleThreshold(int srcX, int srcY) {
  // DG_ScreenBuffer is BGRA (doomgeneric's convention on every backend in
  // this project so far); channel 0 (B) carries the on/off signal for our
  // thresholded monochrome output, matching the netstream/video pipeline's
  // existing convention.
  uint32_t px = DG_ScreenBuffer[srcY * DOOMGENERIC_RESX + srcX];
  uint8_t b = (uint8_t)(px & 0xFF);
  return b >= 128;
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
  // Plain packed-byte memory now (TLManagerNode over a byte-addressable
  // Mem, not the earlier per-word regmap scheme) -- MSB-first per byte,
  // matching the .vidf format's own convention (video2frames.py's
  // pack_1bit), so both DOOM and Bad Apple agree with the VGA hardware's
  // bit ordering without any reversal anywhere.
  volatile uint8_t *fb = (volatile uint8_t *)FRAMEBUFFER_BASE;

  for (int y = 0; y < FB_HEIGHT; y++) {
    int srcY2 = y - FB_ROW_OFFSET_Y; // row within the centered 200-row image
    for (int xByte = 0; xByte < FB_BYTES_PER_ROW; xByte++) {
      uint8_t byteVal = 0;
      for (int bit = 0; bit < 8; bit++) {
        int x = xByte * 8 + bit;
        int on = 0;
        if (srcY2 >= 0 && srcY2 < DOOMGENERIC_RESY / 2) {
          on = sampleThreshold(x * 2, srcY2 * 2);
        }
        byteVal |= (on ? 1u : 0u) << (7 - bit); // MSB-first
      }
      fb[y * FB_BYTES_PER_ROW + xByte] = byteVal;
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
