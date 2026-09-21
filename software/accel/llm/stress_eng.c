// Engine-only stress: repeat a multi-tile GEMM (all transpose modes) and check
// against a C reference every iteration. Prints one '.' per iteration.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#include "../int8_gemm.h"
#define M 16
#define N 24
#define K 32
static int8_t A[K*M] __attribute__((aligned(8))), B[K*N] __attribute__((aligned(8)));
static int32_t C[M*N] __attribute__((aligned(8)));
static uint32_t lcg = 99;
static int8_t rnd8(void){lcg=lcg*1664525u+1013904223u;return (int8_t)(lcg>>24);}
static void put_dec(uint64_t v){char b[24];int n=0;if(!v)b[n++]='0';while(v){b[n++]='0'+v%10;v/=10;}while(n)uart_putc(b[--n]);}
int main(void) {
  uart_init(); uart_puts("\r\n[eng-stress] start\r\n");
  for (int i = 0; i < K*M; i++) A[i] = rnd8();
  for (int i = 0; i < K*N; i++) B[i] = rnd8();
  for (uint32_t it = 1;; it++) {
    int bad = 0;
    for (int mode = 0; mode < 4; mode++) {
      int tA = mode & 1, tB = mode >> 1;
      acc_gemm(M, N, K, A, tA ? M : K, tA, B, tB ? K : N, tB, C, N, 0, 0, 0);
      for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) {
        int32_t s = 0; for (int k = 0; k < K; k++)
          s += (tA ? A[k*M+i] : A[i*K+k]) * (tB ? B[j*K+k] : B[k*N+j]);
        if (C[i*N+j] != s) bad++;
      }
    }
    if (bad) { uart_puts("\r\n[eng-stress] MISMATCH it="); put_dec(it); uart_puts(" n="); put_dec(bad); uart_puts("\r\n"); }
    else uart_putc('.');
    if (it % 40 == 0) { uart_puts(" it="); put_dec(it); uart_puts("\r\n"); }
  }
}
