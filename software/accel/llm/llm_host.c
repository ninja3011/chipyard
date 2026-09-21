// Host harness for llm_core.h (plain-C integer matvec backend).
//   llm_host blob.bin gen  N temp seed      -> generate N tokens from BOS
//   llm_host blob.bin teach t0 t1 t2 ...    -> feed tokens, print argmax after each
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "llm_core.h"
static void emit(const char *s, int n) { fwrite(s, 1, n, stdout); fflush(stdout); }
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); fread(blob, 1, n, f);
  Model m; model_init(&m, blob);
  if (!strcmp(argv[2], "teach")) {
    for (int i = 3; i < argc; i++) { forward(&m, atoi(argv[i]), i - 3); printf("%d ", argmax_logits()); }
    printf("\n"); return 0;
  }
  int steps = atoi(argv[3]); float temp = atof(argv[4]); rng_state = atoi(argv[5]);
  int tok = 1, prev;
  for (int pos = 0; pos < steps && pos < MAXPOS; pos++) {
    forward(&m, tok, pos); prev = tok; tok = sample_logits(temp);
    if (tok == 1) break;
    decode_token(&m, prev, tok, emit);
  }
  printf("\n"); return 0;
}
