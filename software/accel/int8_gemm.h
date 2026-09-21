// Tiled INT8 GEMM on top of the 8x8 tile engine (int8_accel.h).
//   C[MxN] = op(A)[MxK] * op(B)[KxN]      (int32 accumulate)
// op(X) = X or X^T. A/B/C are row-major; lda/ldb/ldc are row strides in
// ELEMENTS of the stored (untransposed) matrix. M, N, K multiples of 8,
// every tile row 8-byte aligned (lda, ldb multiples of 8; ldc even for int32
// output, multiple of 8 for int8 output).
//
// A transposed operand needs no data movement: op(A)[i..i+7][k..k+7] is the
// transpose of stored A[k..k+7][i..i+7], so we load that stored tile and set
// the engine's transpose flag for the MAC (same for B).
#ifndef INT8_GEMM_H
#define INT8_GEMM_H
#include "int8_accel.h"

// out8=0: C is int32*, out8=1: C is int8* with (acc >> shift), optional ReLU,
// saturated to int8 -- ready to be the next layer's INT8 activations.
static inline void acc_gemm(int M, int N, int K,
                            const int8_t *A, int lda, int tA,
                            const int8_t *B, int ldb, int tB,
                            void *C, int ldc, int out8, int shift, int relu) {
  for (int i = 0; i < M; i += 8) {
    for (int j = 0; j < N; j += 8) {
      acc_clear();
      for (int k = 0; k < K; k += 8) {
        const int8_t *a = tA ? A + k * lda + i : A + i * lda + k;
        const int8_t *b = tB ? B + j * ldb + k : B + k * ldb + j;
        acc_cfg(lda, 0); acc_load_a(a);
        acc_cfg(ldb, 0); acc_load_b(b);
        acc_mac(tA, tB);
      }
      if (out8) {
        acc_cfg(0, ldc);
        acc_store8((int8_t *)C + i * ldc + j, shift, relu);
      } else {
        acc_cfg(0, ldc * 4);
        acc_store32((int32_t *)C + i * ldc + j);
      }
    }
  }
  acc_sync();
}
#endif
