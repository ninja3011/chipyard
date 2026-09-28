// Host emulation of smol_arty.c's serial protocol (R/G/T/O/F/C/B), for testing the exact same code path
// (llm_core.h + llm_batch.h) that runs on the chip, without a 28-minute board reload.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MODEL_SMOL
#include "../llm/llm_core.h"
#include "../llm/llm_batch.h"
static int g_lora_ready = 0;
static void put_dec(uint64_t v) { printf("%llu", (unsigned long long)v); }
static void put_fixed(int64_t x, int d) { if (x < 0) { putchar('-'); x = -x; } int64_t p = 1; for (int i=0;i<d;i++) p*=10; printf("%lld.", (long long)(x/p)); for (int64_t q=p/10;q>=1;q/=10) putchar('0'+(x/q)%10); }
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); if (fread(blob, 1, n, f) != (size_t)n) return 1;
  static Model m; model_init(&m, blob); int pos = 0;
  // argv[2]: "T" <epochs> <n_train> id... "G" <maxnew> <temp10> <rep100> <n_prompt> id...
  int i = 2;
  while (i < argc) {
    if (!strcmp(argv[i], "R")) { pos = 0; i++; printf("reset\n"); continue; }
    if (!strcmp(argv[i], "T")) {
      int epochs = atoi(argv[i+1]); int tn = atoi(argv[i+2]); i += 3;
      static int tids[MAXPOS]; for (int k = 0; k < tn; k++) tids[k] = atoi(argv[i++]);
      if (!g_lora_ready) { rng_state = 12345; lora_init(); g_lora_ready = 1; }
      g_lora_on = 1; pos = 0;
      for (int e = 0; e < epochs; e++) {
        float lre = 0.004f * (1.0f - 0.9f * (float)e / (float)epochs);
        float loss = lora_epoch(&m, tids, tn, lre);
        printf("EPOCH %d ", e+1); put_fixed((int64_t)(loss*10000.f),4); printf("\n");
      }
      pos = 0; printf("TRAINDONE\n");
    } else if (!strcmp(argv[i], "O")) { g_lora_on = 0; i++; printf("base\n"); }
    else if (!strcmp(argv[i], "F")) { g_lora_on = g_lora_ready; i++; printf("tuned\n"); }
    else if (!strcmp(argv[i], "G")) {
      int maxnew = atoi(argv[i+1]); int temp10 = atoi(argv[i+2]); int rep100 = atoi(argv[i+3]); int cnt = atoi(argv[i+4]); i += 5;
      static int pids[512]; for (int k = 0; k < cnt; k++) pids[k] = atoi(argv[i++]);
      for (int q = 0; q < cnt; q += NB) { int nb = cnt - q < NB ? cnt - q : NB; forward_batch_ex(&m, pids + q, nb, pos, q + nb >= cnt); pos += nb; }
      int last = pids[cnt-1]; static int hist[128]; int nh = 0;
      rng_state = 999;
      printf("REPLY ");
      for (int g = 0; g < maxnew; g++) {
        if (rep100 > 100) for (int h = 0; h < nh; h++) { float *l=&logits_[hist[h]]; float r=rep100/100.f; *l=(*l>0.f)?*l/r:*l*r; }
        int tok = sample_logits(temp10/10.0f); if (nh<128) hist[nh++]=tok;
        printf("%d ", tok);
        if (pos >= MAXPOS-1) break;
        forward(&m, tok, pos++);
        if (tok == 2 || tok == 0) break;
      }
      printf("\n"); (void)last;
    } else i++;
  }
  return 0;
}
