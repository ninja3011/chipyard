// Verifies the llm_batch.h fix: after LoRA training, forward() (token-by-token) and forward_batch_ex()
// (8-at-a-time) must produce IDENTICAL logits for the last token of a sequence.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MODEL_SMOL
#include "../llm/llm_core.h"
#include "../llm/llm_batch.h"
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); if (fread(blob, 1, n, f) != (size_t)n) return 1;
  static Model m; model_init(&m, blob); int N = argc - 2; static int ids[64];
  for (int i = 0; i < N; i++) ids[i] = atoi(argv[2 + i]);
  rng_state = 12345; lora_init();
  float loss = lora_epoch(&m, ids, N, 0.004f);
  printf("trained 1 epoch, loss=%.4f, g_lora_on=%d\n", loss, g_lora_on);
  static float ref[VOCAB];
  for (int i = 0; i < N; i++) forward(&m, ids[i], i);
  memcpy(ref, logits_, sizeof ref);
  for (int p = 0; p < N; p += NB) { int nb = N - p < NB ? N - p : NB; forward_batch_ex(&m, ids + p, nb, p, p + nb >= N); }
  int diff = 0; for (int i = 0; i < VOCAB; i++) if (ref[i] != logits_[i]) diff++;
  printf("with LoRA on: logits differing (sequential vs batched): %d of %d\n", diff, VOCAB);
  return diff != 0;
}
