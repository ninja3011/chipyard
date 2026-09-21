// Checks acc_gemm (multi-tile, all transpose combos, int32 and int8 output)
// against a plain C reference.
#include <stdint.h>
#include "../doom/baremetal-arty100t/uart.h"
#include "int8_gemm.h"

#define M 16
#define N 24
#define K 32
static int8_t A[K*M] __attribute__((aligned(8)));   // big enough for A or A^T
static int8_t B[K*N] __attribute__((aligned(8)));
static int32_t C32[M*N] __attribute__((aligned(8)));
static int8_t  C8[M*N] __attribute__((aligned(8)));

static uint32_t lcg = 777;
static int8_t rnd8(void) { lcg = lcg * 1664525u + 1013904223u; return (int8_t)(lcg >> 24); }
static void put_dec(int64_t v) {
  char b[24]; int n = 0; if (v < 0) { uart_putc('-'); v = -v; }
  if (!v) b[n++] = '0'; while (v) { b[n++] = '0' + v % 10; v /= 10; } while (n) uart_putc(b[--n]);
}
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }

// op(A)[i][k] with stored layout: tA ? A[k*M+i] : A[i*K+k]
static int aval(int i, int k, int tA) { return tA ? A[k * M + i] : A[i * K + k]; }
static int bval(int k, int j, int tB) { return tB ? B[j * K + k] : B[k * N + j]; }

int main(void) {
  uart_init();
  uart_puts("\r\n[gemm] start\r\n");
  for (int i = 0; i < K * M; i++) A[i] = rnd8();
  for (int i = 0; i < K * N; i++) B[i] = rnd8();
  int fails = 0;
  for (int mode = 0; mode < 4; mode++) {
    int tA = mode & 1, tB = mode >> 1;
    int lda = tA ? M : K, ldb = tB ? K : N;
    uint64_t t0 = rdcycle();
    acc_gemm(M, N, K, A, lda, tA, B, ldb, tB, C32, N, 0, 0, 0);
    uint64_t t1 = rdcycle();
    int bad = 0;
    for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) {
      int32_t s = 0; for (int k = 0; k < K; k++) s += aval(i, k, tA) * bval(k, j, tB);
      if (C32[i * N + j] != s) bad++;
    }
    uart_putc('0' + tA); uart_putc('0' + tB); uart_puts(bad ? " FAIL " : " ok ");
    put_dec(t1 - t0); uart_puts("cyc\r\n"); fails += bad;
  }
  // int8 output with shift+relu
  acc_gemm(M, N, K, A, K, 0, B, N, 0, C8, N, 1, 7, 1);
  int bad = 0;
  for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) {
    int32_t s = 0; for (int k = 0; k < K; k++) s += aval(i, k, 0) * bval(k, j, 0);
    s >>= 7; if (s < 0) s = 0; if (s > 127) s = 127;
    if (C8[i * N + j] != s) bad++;
  }
  uart_puts(bad ? "i8 FAIL\r\n" : "i8 ok\r\n"); fails += bad;
  uint64_t s0 = rdcycle();
  for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) {
    int32_t s = 0; for (int k = 0; k < K; k++) s += aval(i, k, 0) * bval(k, j, 0);
    C32[i * N + j] = s;
  }
  uart_puts("sw "); put_dec(rdcycle() - s0); uart_puts("cyc\r\n");
  uart_puts(fails ? "[gemm] FAIL\r\n" : "[gemm] ALL PASS\r\n");
  while (1) {}
}
