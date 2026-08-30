// Bare-metal Bad Apple player for RocketArty100TVGAConfig, reusing the
// exact same bare-metal foundation built for DOOM (syscalls.c, uart.h,
// the newlib/nano.specs runtime) rather than anything network-based --
// there's no network here, unlike the earlier Linux netstream version.
//
// The .vidf format (software/video/video2frames.py) is already 320x200,
// 1 bit/pixel, packed MSB-first row-major -- almost a direct match for
// the VGA peripheral's native 320x240 1bpp framebuffer. Unlike DOOM's
// path, no thresholding or downsampling is needed, just the same 20-row
// vertical letterbox centering.

#include <stdint.h>
#include "uart.h"

#define FRAMEBUFFER_BASE 0x04000000UL
#define FB_WIDTH 320
#define FB_HEIGHT 240
#define FB_BYTES_PER_ROW (FB_WIDTH / 8)
#define ROW_OFFSET_Y ((FB_HEIGHT - 200) / 2) /* 20-row letterbox, matches DOOM's path */

extern char video_vidf_start[];
extern char video_vidf_end[];

// mtime tick rate confirmed from this build's own generated DTS -- see
// doomgeneric_arty100t.c's header comment for the full story. Same core,
// same real hardware, same real 50kHz answer.
#define MTIME_HZ 50000ULL

static uint64_t rdtime(void) {
  uint64_t t;
  __asm__ volatile ("rdtime %0" : "=r"(t));
  return t;
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
  uint32_t frameBytes = (hdr->width * hdr->height) / 8;
  uint32_t frameIntervalMs = 1000 / hdr->fps;

  volatile uint8_t *fb = (volatile uint8_t *)FRAMEBUFFER_BASE;

  while (1) {
    const uint8_t *frame = frameData;
    for (uint32_t f = 0; f < hdr->numFrames; f++) {
      // The .vidf frame is already exactly hdr->width x hdr->height (320x200),
      // packed MSB-first per row (hdr->width/8 = 40 bytes/row) -- now that
      // the framebuffer is a plain byte-addressable memory using the same
      // MSB-first convention, this is a direct row-by-row copy, no
      // re-packing needed at all.
      for (uint32_t y = 0; y < hdr->height; y++) {
        uint32_t fbY = y + ROW_OFFSET_Y;
        const uint8_t *rowBytes = frame + y * (hdr->width / 8);
        for (int b = 0; b < FB_BYTES_PER_ROW; b++) {
          fb[fbY * FB_BYTES_PER_ROW + b] = rowBytes[b];
        }
      }
      frame += frameBytes;
      sleepMs(frameIntervalMs);
    }
    uart_puts("[badapple] loop\r\n");
  }
  return 0;
}
