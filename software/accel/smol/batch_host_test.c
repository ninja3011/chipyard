// Host test: forward_batch (chunks of 8) must give the SAME logits as token-by-token forward().
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef T15M
#define MODEL_15M
#else
#define MODEL_SMOL
#endif
#include "llm_core.h"
#include "llm_batch.h"
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); if (fread(blob, 1, n, f) != (size_t)n) return 1;
  static Model m; model_init(&m, blob); int ids[64], N = argc - 2; for (int i = 0; i < N; i++) ids[i] = atoi(argv[2 + i]);
  static float ref[VOCAB];
  for (int i = 0; i < N; i++) forward(&m, ids[i], i);
  memcpy(ref, logits_, sizeof ref); int seq_arg = argmax_logits();
  for (int p = 0; p < N; p += NB) { int nb = N - p < NB ? N - p : NB; forward_batch(&m, ids + p, nb, p); }
  int diff = 0; for (int i = 0; i < VOCAB; i++) if (ref[i] != logits_[i]) diff++;
  printf("tokens=%d chunks=%d  logits differing vs sequential: %d of %d   argmax seq=%d batch=%d\n", N, (N + NB - 1) / NB, diff, VOCAB, seq_arg, argmax_logits());
  return diff != 0;
}
