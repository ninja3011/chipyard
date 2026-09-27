// Host (PC) run of the SAME C forward pass the chip uses (plain-C matvec), greedy, from token ids.
//   smol_host smol135_i8.bin <maxnew> id id id ...     -> prints the generated ids
#include <stdio.h>
#include <stdlib.h>
#define MODEL_SMOL
#include "llm_core.h"
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); if (fread(blob, 1, n, f) != (size_t)n) return 1;
  static Model m; model_init(&m, blob); int maxnew = atoi(argv[2]), pos = 0;
  for (int i = 3; i < argc; i++) forward(&m, atoi(argv[i]), pos++);
  for (int g = 0; g < maxnew; g++) { int t = argmax_logits(); printf("%d ", t); fflush(stdout); if (t == 2 || t == 0) break; forward(&m, t, pos++); }
  printf("\n"); return 0;
}
