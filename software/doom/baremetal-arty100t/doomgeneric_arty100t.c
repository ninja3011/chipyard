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
#include "checkpoint.h"
#include <stdint.h>

#ifndef CONSOLE_ASCII_VIDEO

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

#else // CONSOLE_ASCII_VIDEO

// Text-mode fallback for RocketArty100TConfig. Two independent facts
// forced this design, discovered in this order on real hardware:
//
// 1. No VGA peripheral exists in this config at all -- writing to
//    FRAMEBUFFER_BASE would hit an unmapped address and fault on the
//    first frame (see the top-of-file comment; VGA's own clock-crossing
//    bug, found 2026-09-11, is this board's *actual* long-standing
//    CPU-wake root cause, unrelated to DMI/SBA/CLINT -- being fixed
//    separately, this backend exists to make progress without it).
// 2. The console UART this backend originally wrote ASCII art to
//    (uart_puts, same peripheral DG_GetKey polls) is wired to the JA
//    PMOD header's physical pins (WithArty100TJAUART), not the onboard
//    USB-UART bridge the working uart_tsi link uses (A9/D10) -- with no
//    external USB-serial adapter connected to JA on this session's
//    physical setup, that output has no path off the board at all.
//
// So the actual output path here is a plain DRAM scratch buffer instead
// of any UART: each frame is packed 4 ASCII chars/word (so a host-side
// uart_tsi +init_read loop only needs COLS*ROWS/4 reads, not one per
// character) behind a leading sequence-number word a poller can watch to
// know when a new frame has actually landed since its last read, at a
// fixed high DRAM address chosen well clear of the program+WAD+heap
// footprint (the linked image runs to a bit under 0x81c10000).
//
// NOT 0x88000000 (128MB offset): confirmed via a standalone probe payload
// that this board's DRAM only reliably responds up to ~120MB in (tested
// working through 0x87800000, broken at and beyond 0x88000000 -- reads
// there return stale/unrelated data regardless of what's written, most
// likely a MIG/DDR3 configuration limit despite the SoC's memory map
// claiming a full 256MB window). 0x84000000 (64MB offset) is comfortably
// inside the confirmed-good range with wide margin on both sides.
#define ASCII_COLS 64
#define ASCII_ROWS 32
#define CONSOLE_FRAME_ADDR 0x84000000UL
// [0] = sequence number (host polls this to detect a new frame)
// [1..] = ASCII_COLS*ASCII_ROWS bytes, packed 4/word, row-major
static volatile uint32_t * const kConsoleSeq = (volatile uint32_t *)CONSOLE_FRAME_ADDR;
static volatile uint32_t * const kConsoleGrid = (volatile uint32_t *)(CONSOLE_FRAME_ADDR + 4);

// Standard dark-to-light density ramp (10 levels); index by luma>>~5.
static const char kRamp[] = " .:-=+*#%@";
#define RAMP_LEVELS ((int)(sizeof(kRamp) - 1))

static inline uint8_t sampleLuma(int srcX, int srcY) {
  uint32_t px = DG_ScreenBuffer[srcY * DOOMGENERIC_RESX + srcX];
  uint8_t b = (uint8_t)(px >> 0);
  uint8_t g = (uint8_t)(px >> 8);
  uint8_t r = (uint8_t)(px >> 16);
  // BT.601 integer luma weights (77+150+29 == 256).
  return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

#endif // CONSOLE_ASCII_VIDEO

// mtime tick rate: confirmed (not assumed) from this exact config's own
// generated DTS -- `timebase-frequency = <50000>` under /cpus, in
// fpga/generated-src/.../*.dts, generated 2026-08-28. This is NOT the
// common 1MHz convention some rocket-chip configs use; RocketArty100TConfig
// really does run its timebase at 50kHz. Getting this wrong would have
// made DG_SleepMs and DG_GetTicksMs off by 20x (a "1MHz assumed, 50kHz
// real" mismatch makes every sleep 20x longer than intended), so this is
// worth having caught before real hardware time, not during it.
#define MTIME_HZ 50000ULL

// NOT the `rdtime` CPU instruction: verified against this exact core's own
// CSR.scala that the `time` CSR it reads is not implemented here (only
// `cycle`/`instret`/hpmcounterN are in read_mapping) -- executing `rdtime`
// takes an illegal-instruction trap that this bare-metal build has no
// handler for, and the core hangs on that single instruction forever
// (confirmed on real hardware via checkpoint instrumentation: execution
// reaches immediately before this line and never reaches immediately
// after it). CLINT's mtime is the same underlying 50kHz counter
// (MTIME_HZ below), reached instead via an ordinary memory read of a
// peripheral this project has used reliably since day one, with no
// special CPU instruction involved.
#define CLINT_MTIME_ADDR 0x0200BFF8UL

static uint64_t rdtime(void) {
  return *(volatile uint64_t *)CLINT_MTIME_ADDR;
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
  CHECKPOINT(61); /* uart_init() done */
  s_startTimeTicks = rdtime();
  CHECKPOINT(62); /* rdtime() done */
#ifndef CONSOLE_ASCII_VIDEO
  uart_puts("\r\n[doom] arty100t bare-metal backend up (VGA framebuffer @ 0x04000000)\r\n");
#else
  uart_puts("\r\n[doom] arty100t bare-metal backend up (ASCII console video, no VGA)\r\n");
  CHECKPOINT(63); /* first uart_puts() done */
  uart_puts("\033[2J"); // clear screen once; each frame re-homes the cursor instead of re-clearing
  CHECKPOINT(64); /* second uart_puts() done -- end of DG_Init */
#endif
}

#ifndef CONSOLE_ASCII_VIDEO

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

#else // CONSOLE_ASCII_VIDEO

void DG_DrawFrame(void) {
  // Written into DRAM 4 chars/word instead of streamed over UART -- see
  // the CONSOLE_FRAME_ADDR comment above for why (the console UART this
  // originally targeted turned out to be wired to a physically
  // disconnected header on this session's hardware, discovered only
  // after the fact; DRAM has no such dependency, a host machine reads it
  // back directly over the same already-working uart_tsi/DMI link used
  // to load the program in the first place).
  //
  // Not throttled the way the UART path was: writing to local DRAM costs
  // nothing like UART TX time did, so every frame gets written. A host
  // poller decides its own cadence by watching the sequence word; it
  // will simply see whatever the latest completed frame is, same as
  // any other double-buffered display.
  uint32_t word = 0;
  int shift = 0;
  int wordIdx = 0;
  for (int cy = 0; cy < ASCII_ROWS; cy++) {
    int srcY = (cy * DOOMGENERIC_RESY) / ASCII_ROWS;
    for (int cx = 0; cx < ASCII_COLS; cx++) {
      int srcX = (cx * DOOMGENERIC_RESX) / ASCII_COLS;
      uint8_t luma = sampleLuma(srcX, srcY);
      char c = kRamp[(luma * RAMP_LEVELS) >> 8];
      word |= ((uint32_t)(uint8_t)c) << shift;
      shift += 8;
      if (shift == 32) {
        kConsoleGrid[wordIdx++] = word;
        word = 0;
        shift = 0;
      }
    }
  }
  if (shift != 0) kConsoleGrid[wordIdx++] = word; // ASCII_COLS*ASCII_ROWS not a multiple of 4: flush remainder
  // Sequence number last, after the grid it describes is fully written --
  // a poller that only checks *this* word before reading the grid always
  // sees a grid at least as new as the sequence number it just read.
  (*kConsoleSeq)++;
}

#endif // CONSOLE_ASCII_VIDEO

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
  CHECKPOINT(3); /* main() entry */
  static char *fake_argv[] = { "doom", NULL };
  doomgeneric_Create(1, fake_argv);
  while (1) {
    doomgeneric_Tick();
  }
  return 0;
}
