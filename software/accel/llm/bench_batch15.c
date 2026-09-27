// On-chip test: prefill 32 tokens of the 15M model, token-by-token vs batches of 8, both on the engine.
// Checks the final logits are identical and reports cycles and tokens/s for both.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define MODEL_15M
#define USE_ACCEL
#include "llm_core.h"
#include "llm_batch.h"
#include "corpus15b.h"
extern const uint8_t model_blob[];
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }
static void put_dec(uint64_t v) { char b[24]; int n = 0; if (!v) b[n++] = '0'; while (v) { b[n++] = '0' + v % 10; v /= 10; } while (n) uart_putc(b[--n]); }
static void kv(const char *k, uint64_t v) { uart_puts(k); uart_putc('='); put_dec(v); uart_putc(' '); }
int main(void) {
  uart_init(); uart_puts("\r\n[batch] prefill 32 tokens of TinyStories-15M on the engine: one token at a time vs 8 at a time\r\n");
  Model m; model_init(&m, model_blob); g_use_accel = 1; const int N = 32;
  static float ref[VOCAB];
  uint64_t t0 = rdcycle(); for (int i = 0; i < N; i++) forward(&m, corpus[i], i); uint64_t seq = rdcycle() - t0;
  for (int i = 0; i < VOCAB; i++) ref[i] = logits_[i];
  t0 = rdcycle(); for (int p = 0; p < N; p += NB) forward_batch(&m, corpus + p, NB, p); uint64_t bat = rdcycle() - t0;
  int diff = 0; for (int i = 0; i < VOCAB; i++) if (ref[i] != logits_[i]) diff++;
  kv("tokens", N); kv("seq_cyc", seq); kv("batch_cyc", bat); kv("logits_compared", VOCAB); kv("logits_differ", diff);
  uart_puts("\r\nseq_tok_per_s_x100="); put_dec((uint64_t)N * 5000000000ULL / seq);
  uart_puts(" batch_tok_per_s_x100="); put_dec((uint64_t)N * 5000000000ULL / bat);
  uart_puts(" speedup_x100="); put_dec(seq * 100 / bat); uart_puts("\r\n[batch] DONE\r\n");
  for (;;) {}
}
