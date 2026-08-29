// doomgeneric backend for bare-metal RocketArty100TConfig.
//
// Video: DG_DrawFrame writes DG_ScreenBuffer to a memory-mapped framebuffer
// that DOES NOT EXIST YET in the current elaborated design (confirmed via
// the real address map generated 2026-08-28 -- there is no framebuffer
// peripheral in RocketArty100TConfig today). FRAMEBUFFER_BASE below is a
// placeholder; it must be replaced with the real address once the Pmod VGA
// timing-generator peripheral (ARTY-A7-100T-PROJECT-SCOPE.md, Tier 1b) is
// designed and elaborated. Everything else in this file does not depend on
// that peripheral and is real, not a placeholder.
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

// TODO(hardware): replace once the VGA framebuffer peripheral exists.
// See ARTY-A7-100T-PROJECT-SCOPE.md, Tier 1b.
#define FRAMEBUFFER_BASE 0x00000000UL /* placeholder -- not a real address */
#define FRAMEBUFFER_READY 0

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
  uart_puts("\r\n[doom] arty100t bare-metal backend up\r\n");
  if (!FRAMEBUFFER_READY) {
    uart_puts("[doom] WARNING: no framebuffer peripheral yet -- rendering is happening but not visible\r\n");
  }
}

void DG_DrawFrame(void) {
  if (!FRAMEBUFFER_READY) return; // nothing to draw to yet, see header comment
  volatile uint32_t *fb = (volatile uint32_t *)FRAMEBUFFER_BASE;
  const uint32_t *src = (const uint32_t *)DG_ScreenBuffer;
  for (int i = 0; i < DOOMGENERIC_RESX * DOOMGENERIC_RESY; i++) {
    fb[i] = src[i];
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
