// Same as smol_train_host.c but also generates on a SEPARATE held-out prompt after training, to check
// whether the adapter generalizes beyond the exact trained sentence.
//   smol_train_host2 blob.bin lr epochs <n_train> id... -- <n_prompt2> id...
#include <stdio.h>
#include <stdlib.h>
#define MODEL_SMOL
#include "../llm/llm_core.h"
static void gen_ids(const Model *m, const int *prompt, int np, int maxnew) {
  int pos = 0;
  for (int i = 0; i < np; i++) forward(m, prompt[i], pos++);
  for (int i = 0; i < maxnew; i++) { int t = argmax_logits(); printf("%d ", t); if (t == 2 || t == 0) break; forward(m, t, pos++); }
  printf("\n");
}
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); if (fread(blob, 1, n, f) != (size_t)n) return 1;
  static Model m; model_init(&m, blob);
  float lr = atof(argv[2]); int epochs = atoi(argv[3]);
  int i = 4; int nt = atoi(argv[i++]); static int train_ids[MAXPOS];
  for (int k = 0; k < nt; k++) train_ids[k] = atoi(argv[i++]);
  i++; /* skip "--" */
  int np2 = atoi(argv[i++]); static int prompt2[MAXPOS];
  for (int k = 0; k < np2; k++) prompt2[k] = atoi(argv[i++]);
  printf("BEFORE_TRAINED_PROMPT "); gen_ids(&m, train_ids, 27, 40);
  printf("BEFORE_PARAPHRASE     "); gen_ids(&m, prompt2, np2, 40);
  rng_state = 12345; lora_init();
  for (int e = 0; e < epochs; e++) {
    float lre = lr * (1.0f - 0.9f * (float)e / (float)epochs);
    float loss = lora_epoch(&m, train_ids, nt, lre);
    if (e < 3 || e % 5 == 4 || e == epochs - 1) fprintf(stderr, "epoch %2d  loss %.4f\n", e + 1, loss);
  }
  printf("AFTER_TRAINED_PROMPT  "); gen_ids(&m, train_ids, 27, 40);
  printf("AFTER_PARAPHRASE      "); gen_ids(&m, prompt2, np2, 40);
  return 0;
}
