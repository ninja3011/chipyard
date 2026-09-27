// Host harness: LoRA fine-tune on a token sequence, then compare generations.
//   llm_train_host blob.bin lr epochs t0 t1 t2 ...
#include <stdio.h>
#include <stdlib.h>
#include "llm_core.h"
static void emit(const char *s, int n) { fwrite(s, 1, n, stdout); }
static void gen(const Model *m, int steps) {
  if (getenv("CLEAN")) { gen_sentences(m, steps, emit); return; }
  int tok = 1, prev;
  for (int pos = 0; pos < steps; pos++) {
    forward(m, tok, pos); prev = tok; tok = argmax_logits(); if (tok == 1 || tok == 2) break;
    decode_token(m, prev, tok, emit);
  }
  printf("\n");
}
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); if (fread(blob, 1, n, f) != (size_t)n) return 1;
  Model m; model_init(&m, blob);
  float lr = atof(argv[2]); int epochs = atoi(argv[3]); int nt = argc - 4; int toks[MAXPOS];
  for (int i = 0; i < nt; i++) toks[i] = atoi(argv[4 + i]);
  printf("--- before training ---\n"); gen(&m, 220);
  rng_state = 12345; lora_init();
  for (int e = 0; e < epochs; e++) {
    float lre = lr * (1.0f - 0.9f * (float)e / (float)epochs);  /* linear decay to 10% */
    float loss = lora_epoch(&m, toks, nt, lre);
    if (e < 3 || e % 10 == 9 || e == epochs - 1) printf("epoch %2d  loss %.4f\n", e + 1, loss);
  }
  printf("--- after training ---\n"); gen(&m, 220);
  return 0;
}
