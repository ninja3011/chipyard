// Self-checking test for the INT8 tile engine: every command variant is
// compared against a plain-C reference. Prints PASS/FAIL over the UART.
#include <stdint.h>
#include "../doom/baremetal-arty100t/uart.h"
#include "int8_accel.h"

static void put_dec(int64_t v) {
  char buf[24]; int n = 0;
  if (v < 0) { uart_putc('-'); v = -v; }
  if (v == 0) buf[n++] = '0';
  while (v > 0) { buf[n++] = '0' + (v % 10); v /= 10; }
  while (n) uart_putc(buf[--n]);
}
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }

static int8_t A[8][8] __attribute__((aligned(8)));
static int8_t B[8][8] __attribute__((aligned(8)));
static int32_t C32[8][8] __attribute__((aligned(8)));
static int8_t C8[8][8] __attribute__((aligned(8)));
static int32_t ref[8][8];

static uint32_t lcg = 12345;
static int8_t rnd8(void) { lcg = lcg * 1664525u + 1013904223u; return (int8_t)(lcg >> 24); }

static void ref_mac(int tA, int tB, int clear) {
  for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) {
    int32_t s = clear ? 0 : ref[i][j];
    for (int k = 0; k < 8; k++) {
      int a = tA ? A[k][i] : A[i][k];
      int b = tB ? B[j][k] : B[k][j];
      s += a * b;
    }
    ref[i][j] = s;
  }
}
static int check32(const char *name) {
  int bad = 0;
  for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) if (C32[i][j] != ref[i][j]) bad++;
  uart_puts(name); uart_puts(bad ? ": FAIL (" : ": ok");
  if (bad) { put_dec(bad); uart_puts(" wrong)"); }
  uart_puts("\r\n");
  return bad;
}

int main(void) {
  uart_init();
  uart_puts("\r\n[accel_test] start\r\n");
  int fails = 0;
  for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) { A[i][j] = rnd8(); B[i][j] = rnd8(); }

  acc_cfg(8, 32);
  for (int mode = 0; mode < 4; mode++) {
    int tA = mode & 1, tB = mode >> 1;
    acc_load_a(A); acc_load_b(B); acc_clear(); acc_mac(tA, tB); acc_store32(C32); acc_sync();
    ref_mac(tA, tB, 1);
    static const char *names[4] = {"mac A*B", "mac At*B", "mac A*Bt", "mac At*Bt"};
    fails += check32(names[mode]);
  }

  // accumulate across two MACs (K = 16 style)
  acc_load_a(A); uart_putc('1'); acc_load_b(B); uart_putc('2'); acc_clear(); uart_putc('3');
  acc_mac(0, 0); uart_putc('4');
  acc_mac(0, 0); uart_putc('5'); acc_store32(C32); uart_putc('6'); acc_sync(); uart_putc('7');
  ref_mac(0, 0, 1); ref_mac(0, 0, 0); uart_putc('8');
  fails += check32("accumulate x2");

  // ST8: shift + relu + saturate
  for (int variant = 0; variant < 2; variant++) {
    int shift = 6, relu = variant;
    acc_load_a(A); acc_load_b(B); acc_clear(); acc_mac(0, 0);
    acc_cfg(8, 8); acc_store8(C8, shift, relu); acc_sync(); acc_cfg(8, 32);
    ref_mac(0, 0, 1);
    int bad = 0;
    for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) {
      int32_t v = ref[i][j] >> shift;
      if (relu && v < 0) v = 0;
      if (v > 127) v = 127;
      if (v < -128) v = -128;
      if (C8[i][j] != v) bad++;
    }
    uart_puts(relu ? "st8 relu" : "st8"); uart_puts(bad ? ": FAIL (" : ": ok");
    if (bad) { put_dec(bad); uart_puts(" wrong)"); }
    uart_puts("\r\n");
    fails += bad;
  }

  // Speed: engine vs core doing the same 8x8x8 multiply in software
  uint64_t t0 = rdcycle();
  acc_load_a(A); acc_load_b(B); acc_clear(); acc_mac(0, 0); acc_store32(C32); acc_sync();
  uint64_t t1 = rdcycle();
  ref_mac(0, 0, 1);
  uint64_t t2 = rdcycle();
  uart_puts("engine cycles (load+mac+store): "); put_dec(t1 - t0);
  uart_puts("\r\nsoftware cycles: "); put_dec(t2 - t1); uart_puts("\r\n");

  uart_puts(fails ? "[accel_test] FAIL\r\n" : "[accel_test] ALL PASS\r\n");
  while (1) {}
}
