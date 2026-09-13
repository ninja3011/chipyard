// Bare-metal Bad Apple player for RocketArty100TVGAConfig, reusing the
// exact same bare-metal foundation built for DOOM (syscalls.c, uart.h,
// the newlib/nano.specs runtime) rather than anything network-based --
// there's no network here, unlike the earlier Linux netstream version.
//
// The .vidf format (software/video/video2frames.py) is 320x200, 1 bit/
// pixel, packed MSB-first row-major. The VGA peripheral is real 8-bit
// color (3-3-2 RGB, see VGAFramebuffer.scala's header comment for why),
// so each source bit is expanded on the fly: on -> full white
// (R=7,G=7,B=3, the actual max value each field can hold), off -> full
// black -- keeps Bad Apple's real black and white identity while
// exercising the real color write path end to end. Same 20-row vertical
// letterbox centering as before.

#include <stdint.h>
#include "uart.h"

#define FRAMEBUFFER_BASE 0x04000000UL
#define FB_WIDTH 320
#define FB_HEIGHT 240
#define FB_BYTES_PER_PIXEL 1
#define FB_BYTES_PER_ROW (FB_WIDTH * FB_BYTES_PER_PIXEL)
#define ROW_OFFSET_Y ((FB_HEIGHT - 200) / 2) /* 20-row letterbox, matches DOOM's path */

#define PIXEL_WHITE 0xFFU /* R=7,G=7,B=3 -> (7<<5)|(7<<2)|3 = 0xFF, the real per-field max */
#define PIXEL_BLACK 0x00U

extern char video_vidf_start[];
extern char video_vidf_end[];

// mtime tick rate confirmed from this build's own generated DTS -- see
// doomgeneric_arty100t.c's header comment for the full story. Same core,
// same real hardware, same real 50kHz answer.
#define MTIME_HZ 50000ULL

// NOT the `rdtime` CPU instruction: this core doesn't implement the `time`
// CSR it reads (confirmed against rocket-chip's own CSR.scala -- only
// cycle/instret/hpmcounterN are in read_mapping), so executing it takes an
// illegal-instruction trap this bare-metal build has no handler for and
// hangs forever. See doomgeneric_arty100t.c's rdtime() for the full story
// (found and fixed there first, via on-hardware checkpoint instrumentation).
// Same fix here: read CLINT's mtime directly instead, same 50kHz counter,
// no special instruction.
#define CLINT_MTIME_ADDR 0x0200BFF8UL

static uint64_t rdtime(void) {
  return *(volatile uint64_t *)CLINT_MTIME_ADDR;
}

static void sleepMs(uint32_t ms) {
  uint64_t target = rdtime() + ((uint64_t)ms * MTIME_HZ) / 1000ULL;
  while (rdtime() < target) { }
}

typedef struct {
  char magic[4];
  uint32_t width;
  uint32_t height;
  uint32_t numFrames;
  uint32_t fps;
} __attribute__((packed)) VidfHeader;

int main(void) {
  uart_init();
  uart_puts("\r\n[badapple] arty100t bare-metal player up\r\n");

  const VidfHeader *hdr = (const VidfHeader *)video_vidf_start;
  if (hdr->magic[0] != 'V' || hdr->magic[1] != 'I' || hdr->magic[2] != 'D' || hdr->magic[3] != 'F') {
    uart_puts("[badapple] ERROR: bad magic in embedded .vidf\r\n");
    while (1) { }
  }

  uart_puts("[badapple] loaded, playing\r\n");

  const uint8_t *frameData = (const uint8_t *)video_vidf_start + sizeof(VidfHeader);
  uint32_t srcBytesPerRow = hdr->width / 8;
  uint32_t frameBytes = (hdr->width * hdr->height) / 8;
  uint32_t frameIntervalMs = 1000 / hdr->fps;

  volatile uint8_t *fb = (volatile uint8_t *)FRAMEBUFFER_BASE;

  while (1) {
    const uint8_t *frame = frameData;
    for (uint32_t f = 0; f < hdr->numFrames; f++) {
      // Expand each packed 1bpp source pixel into a real 8-bit color
      // pixel (white or black) -- same encoding DOOM's path writes and
      // VGAFramebuffer.scala reads.
      for (uint32_t y = 0; y < hdr->height; y++) {
        uint32_t fbY = y + ROW_OFFSET_Y;
        const uint8_t *rowBytes = frame + y * srcBytesPerRow;
        for (uint32_t x = 0; x < hdr->width; x++) {
          uint8_t srcByte = rowBytes[x / 8];
          int on = (srcByte >> (7 - (x % 8))) & 1;
          fb[fbY * FB_BYTES_PER_ROW + x * FB_BYTES_PER_PIXEL] = on ? PIXEL_WHITE : PIXEL_BLACK;
        }
      }
      frame += frameBytes;
      sleepMs(frameIntervalMs);
    }
    uart_puts("[badapple] loop\r\n");
  }
  return 0;
}
