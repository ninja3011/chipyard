// Host LoRA fine-tune test for SmolLM2-135M-Instruct: same lora_epoch() used for TinyStories, pointed at
// SmolLM's dimensions. Prints generated TOKEN IDS (SmolLM's tokenizer lives on the PC, not in the blob --
// decode separately, e.g. with decode_ids.py).
//   smol_train_host smol135_i8.bin lr epochs <n_prompt_tokens> id id id ...
// The first n_prompt_tokens ids are the held-out prompt (used for before/after generation); the REST of the
// ids (prompt + reply) are the full training example.
#include <stdio.h>
#include <stdlib.h>
#define MODEL_SMOL
#include "../llm/llm_core.h"
static void gen_ids(const Model *m, const int *prompt, int np, int maxnew) {
  int pos = 0;
  for (int i = 0; i < np; i++) forward(m, prompt[i], pos++);
  for (int i = 0; i < maxnew; i++) {
    int t = argmax_logits(); printf("%d ", t);
    if (t == 2 || t == 0) break;
    forward(m, t, pos++);
  }
  printf("\n");
}
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); if (fread(blob, 1, n, f) != (size_t)n) return 1;
  static Model m; model_init(&m, blob);
  float lr = atof(argv[2]); int epochs = atoi(argv[3]); int np = atoi(argv[4]);
  static int ids[MAXPOS]; int nt = argc - 5;
  for (int i = 0; i < nt; i++) ids[i] = atoi(argv[5 + i]);
  printf("BEFORE "); gen_ids(&m, ids, np, 40);
  rng_state = 12345; lora_init();
  for (int e = 0; e < epochs; e++) {
    float lre = lr * (1.0f - 0.9f * (float)e / (float)epochs);
    float loss = lora_epoch(&m, ids, nt, lre);
    if (e < 3 || e % 5 == 4 || e == epochs - 1) fprintf(stderr, "epoch %2d  loss %.4f\n", e + 1, loss);
  }
  printf("AFTER  "); gen_ids(&m, ids, np, 40);
  return 0;
}
