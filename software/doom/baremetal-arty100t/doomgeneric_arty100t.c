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

// Text-mode backend for RocketArty100TConfig: no VGA peripheral exists in
// this config at all (writing to FRAMEBUFFER_BASE would hit an unmapped
// address and fault on the first frame -- VGA's own clock-crossing bug,
// found 2026-09-11, is this board's *actual* long-standing CPU-wake root
// cause, unrelated to DMI/SBA/CLINT, and is being fixed separately). This
// backend renders to plain ASCII text over the JA PMOD header UART
// instead (uart_puts, same peripheral DG_GetKey polls for input), via a
// real USB-serial adapter wired to those pins.
//
// (Earlier in this project, before that adapter was connected, this same
// output was instead packed into a DRAM scratch buffer at 0x84000000 and
// polled from the host over uart_tsi -- useful for proving the engine
// renders real frames at all, but a ~50-minute debug-reload per readback
// is nowhere near watchable. Now that there's a live UART link, that
// workaround is gone in favor of streaming directly.)
#define ASCII_COLS 64
#define ASCII_ROWS 32

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


#ifdef CONSOLE_COLOR_VIDEO
// Color protocol v2: every cell is TWO characters carrying 12-bit RGB444
// (R4 G4 B4). v = (r<<8)|(g<<4)|b; c1 = 0x40 + (v>>6), c2 = 0x40 + (v&0x3f),
// so both are in 0x40..0x7f -- never ESC/CR/LF, so the frame framing
// (ESC[H, then ASCII_ROWS lines ending \r\n) is unchanged and unambiguous.
// The cell color is a box average of the source pixels under the cell
// (every 2nd pixel in x, every 3rd in y), not a single point sample.
static inline void sampleCellColor(int cx, int cy, char *c1, char *c2) {
  int x0 = (cx * DOOMGENERIC_RESX) / ASCII_COLS, x1 = ((cx + 1) * DOOMGENERIC_RESX) / ASCII_COLS;
  int y0 = (cy * DOOMGENERIC_RESY) / ASCII_ROWS, y1 = ((cy + 1) * DOOMGENERIC_RESY) / ASCII_ROWS;
  uint32_t sr = 0, sg = 0, sb = 0, n = 0;
  for (int y = y0; y < y1; y += 3) {
    for (int x = x0; x < x1; x += 2) {
      uint32_t px = DG_ScreenBuffer[y * DOOMGENERIC_RESX + x];
      sb += (px >> 0) & 0xff; sg += (px >> 8) & 0xff; sr += (px >> 16) & 0xff; n++;
    }
  }
  uint32_t v = (((sr / n) >> 4) << 8) | (((sg / n) >> 4) << 4) | ((sb / n) >> 4);
  *c1 = (char)(0x40 + (v >> 6));
  *c2 = (char)(0x40 + (v & 0x3f));
}
#endif


#ifdef CONSOLE_PAL_VIDEO
// Video protocol v3 (palette + checksummed row packets). Each frame is a PALETTE
// packet followed by BIN_ROWS ROW packets, so a wire error costs one row (the
// viewer keeps that row from the previous frame) instead of a whole frame.
//   packet  = D0 0D | type | body | s1 s2       (s1,s2 = Fletcher-16 over type+body)
//   PALETTE : type 2, body = 768 bytes (DOOM's gamma-corrected r,g,b for indices 0..255)
//   ROW     : type 1, body = mode(1) + row(1) + W palette indices (W,H from the mode table)
// The picture is DOOM's own 320x200 8-bit buffer (I_VideoBuffer) sampled
// down to the selected size: exact palette indices, no color conversion.
extern unsigned char *I_VideoBuffer;
extern unsigned char dg_palette[768];
static void pumpUartRx(void);   /* defined with the input code below */
#define SRC_W 320
#define SRC_H 200
// Selectable at runtime: the host sends '[' / ']' (never passed to the game) to step down / up.
#define NMODES 5
static const uint16_t kModeW[NMODES] = {160, 192, 224, 256, 320};
static const uint16_t kModeH[NMODES] = {100, 120, 140, 160, 200};
static int s_mode = 1;            // start at 192x120
static int s_pendingMode = -1;    // applied at the next frame boundary
static int s_modeReady = 0;
static uint16_t s_xmap[SRC_W];    // source column for each output column, for the current mode
static void setMode(int m) {
  s_mode = m;
  for (int x = 0; x < kModeW[m]; x++) s_xmap[x] = (uint16_t)((x * SRC_W) / kModeW[m]);
}

static uint32_t s_f1, s_f2;
static inline void pktByte(uint8_t b) {
  s_f1 += b; if (s_f1 >= 255) s_f1 -= 255;
  s_f2 += s_f1; if (s_f2 >= 255) s_f2 -= 255;
  uart_putc((char)b);
}
static inline void pktStart(uint8_t type) {
  uart_putc((char)0xD0); uart_putc((char)0x0D);
  s_f1 = 0; s_f2 = 0;
  pktByte(type);
}
static inline void pktEnd(void) { uart_putc((char)s_f1); uart_putc((char)s_f2); }

static void palDrawFrame(void) {
  if (!s_modeReady) { setMode(s_mode); s_modeReady = 1; }
  if (s_pendingMode >= 0) { setMode(s_pendingMode); s_pendingMode = -1; }
  const int W = kModeW[s_mode], H = kModeH[s_mode];
  pktStart(2);
  for (int i = 0; i < 768; i++) pktByte(dg_palette[i]);
  pktEnd();
  pumpUartRx();
  for (int cy = 0; cy < H; cy++) {
    const unsigned char *src = I_VideoBuffer + ((cy * SRC_H) / H) * SRC_W;
    pktStart(1);
    pktByte((uint8_t)s_mode);
    pktByte((uint8_t)cy);
    for (int cx = 0; cx < W; cx++) pktByte(src[s_xmap[cx]]);
    pktEnd();
    pumpUartRx();   // keep the small RX FIFO drained while we are busy transmitting
  }
}
#endif // CONSOLE_PAL_VIDEO
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
    case ',':           return KEY_STRAFE_L;
    case '.':           return KEY_STRAFE_R;
    case ' ':           return KEY_FIRE;
    case 'e': case 'E': return KEY_USE;
    case 'r': case 'R': return KEY_RSHIFT;   // run
    case 9:              return KEY_TAB;      // automap
    case 13: case 10:   return KEY_ENTER;
    case 27:             return KEY_ESCAPE;
    default:
      if (c >= '0' && c <= '9') return c;     // weapon select, menu numbers
      if (c == 'y' || c == 'Y') return 'y';   // menu confirmations
      if (c == 'n' || c == 'N') return 'n';
      return 0;
  }
}

// A serial link has no key-up. Each received byte (re-sent by the terminal's
// auto-repeat while a key is held) keeps that key "down" for HOLD_MS after the
// LAST byte, then a release is synthesized. The previous version queued a
// press AND a release for every byte, which DOOM sees in the same tic, so
// held-state movement never registered.
#define HOLD_MS 220
#define MAX_DOWN 8
static unsigned char s_downKey[MAX_DOWN];
static uint32_t s_downUntil[MAX_DOWN];
static unsigned char s_isDown[MAX_DOWN];

static void pumpUartRx(void);

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
  // Streamed live over the JA UART now that a real USB-serial adapter is
  // wired to it -- the DRAM-scratch-buffer workaround (writing packed
  // ASCII into 0x84000000, polled over uart_tsi) only existed because
  // that UART was physically disconnected earlier in this project; it
  // proved the engine renders real frames, but a ~50-minute debug-reload
  // per readback is nowhere near watchable, let alone playable.
  //
  // Throttled to every 3rd call: DOOM's internal tic rate is 35Hz, but at
  // 115200 baud a full ASCII_COLS x ASCII_ROWS frame (~2200 bytes incl.
  // line endings) caps out around 5-6 frames/sec of real UART throughput.
  // Sending every frame would just make UART TX time the new bottleneck
  // and back game logic up behind it; this keeps game logic at full rate
  // while the console only shows what the link can actually carry.
  static int frameCounter = 0;
  CHECKPOINT_VALUE(101, (uint32_t)(uintptr_t)DG_ScreenBuffer); /* pointer value at DG_DrawFrame entry -- compare against slot 100 (right after malloc) to tell a NULL-from-the-start malloc failure apart from later corruption */
#ifndef FRAME_EVERY
#define FRAME_EVERY 3
#endif
  if ((frameCounter++ % FRAME_EVERY) != 0) return;

#ifdef CONSOLE_PAL_VIDEO
  palDrawFrame();
  return;
#endif
#ifdef CONSOLE_COLOR_VIDEO
  static char crow[ASCII_COLS * 2 + 3];
  uart_puts("\033[H");
  for (int cy = 0; cy < ASCII_ROWS; cy++) {
    for (int cx = 0; cx < ASCII_COLS; cx++) sampleCellColor(cx, cy, &crow[2 * cx], &crow[2 * cx + 1]);
    crow[ASCII_COLS * 2] = '\r'; crow[ASCII_COLS * 2 + 1] = '\n'; crow[ASCII_COLS * 2 + 2] = '\0';
    uart_puts(crow);
    pumpUartRx(); // keep the small RX FIFO drained while we spend time transmitting
  }
  return;
#endif
  char line[ASCII_COLS + 3]; // + \r \n \0
  uart_puts("\033[H"); // cursor home, no clear -- avoids full-screen flicker
  for (int cy = 0; cy < ASCII_ROWS; cy++) {
    int srcY = (cy * DOOMGENERIC_RESY) / ASCII_ROWS;
    for (int cx = 0; cx < ASCII_COLS; cx++) {
      int srcX = (cx * DOOMGENERIC_RESX) / ASCII_COLS;
      uint8_t luma = sampleLuma(srcX, srcY);
      line[cx] = kRamp[(luma * RAMP_LEVELS) >> 8];
    }
    line[ASCII_COLS] = '\r';
    line[ASCII_COLS + 1] = '\n';
    line[ASCII_COLS + 2] = '\0';
    uart_puts(line);
  }
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

static uint32_t s_rawByteCount = 0; /* every byte ever seen on RX, mapped or not -- tests whether the idle/unattended RX line is producing phantom bytes (electrical noise, no real keyboard attached) that could be injecting random input and explaining why otherwise-identical runs crash at different code paths */

static void pumpUartRx(void) {
  uint32_t now = DG_GetTicksMs();
  int c;
  while ((c = uart_getc_nonblock()) >= 0) {
    s_rawByteCount++;
    CHECKPOINT_VALUE(115, s_rawByteCount); /* cumulative RX byte count */
#ifdef CONSOLE_PAL_VIDEO
    if (c == '[' || c == ']') {           // resolution step, not a game key
      int m = (s_pendingMode >= 0 ? s_pendingMode : s_mode) + (c == ']' ? 1 : -1);
      if (m >= 0 && m < NMODES) s_pendingMode = m;
      continue;
    }
#endif
    unsigned char dk = mapByteToDoomKey((unsigned char)c);
    if (dk == 0) continue;
    int slot = -1;
    for (int i = 0; i < MAX_DOWN; i++) if (s_isDown[i] && s_downKey[i] == dk) { slot = i; break; }
    if (slot >= 0) { s_downUntil[slot] = now + HOLD_MS; continue; }   // already down: extend
    for (int i = 0; i < MAX_DOWN; i++) if (!s_isDown[i]) { slot = i; break; }
    if (slot < 0) continue;                                            // too many keys at once
    s_isDown[slot] = 1; s_downKey[slot] = dk; s_downUntil[slot] = now + HOLD_MS;
    pushKeyEvent(1, dk);
  }
  for (int i = 0; i < MAX_DOWN; i++) {
    if (s_isDown[i] && (int32_t)(now - s_downUntil[i]) >= 0) {
      s_isDown[i] = 0;
      pushKeyEvent(0, s_downKey[i]);
    }
  }
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
  pumpUartRx();
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
