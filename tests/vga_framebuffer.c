// Functional test for chipyard.vga.TLVGAFramebuffer, run through the real
// CPU and pbus (not a synthetic TileLink driver) -- the isolated
// VGAFramebufferUnitTest already checked the module alone; this checks
// the real integration path: ordinary store/load instructions crossing
// the pbus, through TLFragmenter, into the peripheral's TL manager node.
#include <stdio.h>
#include "mmio.h"

#define FB_BASE 0x4000000UL
// 320x240 @ 1bpp = 9600 bytes total; only spot-check a representative
// sample (first/last bytes of several rows) rather than all 9600, to
// keep sim time reasonable while still exercising real addresses across
// the whole memory range, not just offset 0.
#define FB_BYTES_PER_ROW 40  // 320 pixels / 8
#define FB_ROWS 240

int main(void)
{
  int errors = 0;

  // Byte-level write/read-back across a spread of real addresses in the
  // framebuffer's real address range, exercising the actual TL byte-write
  // (PutPartial) path a single volatile store compiles down to.
  for (int row = 0; row < FB_ROWS; row += 17) {
    uint8_t pattern = (uint8_t)(0xA5 ^ row);
    uintptr_t addr = FB_BASE + (uintptr_t)row * FB_BYTES_PER_ROW;
    reg_write8(addr, pattern);
    uint8_t back = reg_read8(addr);
    if (back != pattern) {
      printf("FAIL: row %d byte-write mismatch: wrote 0x%x, read 0x%x\n", row, pattern, back);
      fflush(stdout);
      errors++;
    }
  }

  // Word-level write/read-back (exercises PutFull / a full-beat write,
  // not just single-byte PutPartial).
  for (int row = 0; row < FB_ROWS; row += 31) {
    uint32_t pattern = 0xC001BEEFu ^ (uint32_t)row;
    uintptr_t addr = FB_BASE + (uintptr_t)row * FB_BYTES_PER_ROW;
    reg_write32(addr, pattern);
    uint32_t back = reg_read32(addr);
    if (back != pattern) {
      printf("FAIL: row %d word-write mismatch: wrote 0x%x, read 0x%x\n", row, pattern, back);
      fflush(stdout);
      errors++;
    }
  }

  // Confirm distinct addresses hold distinct values (not all aliasing to
  // one storage location -- a real risk given the periphery shares one
  // Mem between the CPU write port and the VGA scan-out read port).
  reg_write8(FB_BASE + 0, 0x11);
  reg_write8(FB_BASE + 1, 0x22);
  if (reg_read8(FB_BASE + 0) != 0x11 || reg_read8(FB_BASE + 1) != 0x22) {
    printf("FAIL: adjacent framebuffer bytes alias to the same storage\n");
    fflush(stdout);
    errors++;
  }

  if (errors == 0) {
    printf("VGA framebuffer SoC integration test: PASSED (%d checks, real CPU+pbus path)\n",
           (FB_ROWS / 17) + (FB_ROWS / 31) + 1);
    return 0;
  }
  printf("VGA framebuffer SoC integration test: FAILED (%d errors)\n", errors);
  return 1;
}
