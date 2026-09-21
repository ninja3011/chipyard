// Bare-metal INT8 LLM (TinyStories 260K) on Rocket + the 8x8 tile engine.
// Streams generated text over the UART, self-checks that the engine's
// integer results are bit-identical to plain C, and reports tokens/s for
// both backends measured on the same core.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define USE_ACCEL
#include "llm_core.h"

extern const uint8_t model_blob[];

static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }
static void put_dec(uint64_t v) {
  char b[24]; int n = 0; if (!v) b[n++] = '0'; while (v) { b[n++] = '0' + v % 10; v /= 10; } while (n) uart_putc(b[--n]);
}
static void put_fixed2(uint64_t hundredths) {  // 1234 -> "12.34"
  put_dec(hundredths / 100); uart_putc('.'); uint64_t f = hundredths % 100;
  uart_putc('0' + f / 10); uart_putc('0' + f % 10);
}
static void emit(const char *s, int n) {
  for (int i = 0; i < n; i++) { if (s[i] == '\n') uart_putc('\r'); uart_putc(s[i]); }
}
#define CPU_HZ 50000000ULL

int main(void) {
  uart_init();
  uart_puts("\r\n[llm] TinyStories-260K, INT8, on Rocket + 8x8 tile engine\r\n");
  Model m; model_init(&m, model_blob);

  // 1) bit-exactness: same token through engine and through plain C
  static float ref_logits[VOCAB];
  g_use_accel = 1; forward(&m, 1, 0);
  for (int i = 0; i < VOCAB; i++) ref_logits[i] = logits_[i];
  g_use_accel = 0; forward(&m, 1, 0);
  int diff = 0;
  for (int i = 0; i < VOCAB; i++) if (ref_logits[i] != logits_[i]) diff++;
  uart_puts(diff ? "[llm] engine vs software logits: MISMATCH (" : "[llm] engine vs software logits: identical (512/512)\r\n");
  if (diff) { put_dec(diff); uart_puts(" differ)\r\n"); }

  // 2) speed: 16 greedy tokens each way, identical work
  uint64_t cyc[2];
  for (int b = 0; b < 2; b++) {
    g_use_accel = b; int tok = 1; uint64_t t0 = rdcycle();
    for (int pos = 0; pos < 16; pos++) { forward(&m, tok, pos); tok = argmax_logits(); }
    cyc[b] = rdcycle() - t0;
  }
  uart_puts("[llm] 16 tokens: software "); put_dec(cyc[0] / 1000); uart_puts(" kcyc, engine ");
  put_dec(cyc[1] / 1000); uart_puts(" kcyc -> ");
  put_fixed2(cyc[0] * 100 / cyc[1]); uart_puts("x faster\r\n");
  uart_puts("[llm] engine: "); put_fixed2(16 * CPU_HZ * 100 / cyc[1]); uart_puts(" tokens/s\r\n\r\n");

  // 3) generate stories forever
  g_use_accel = 1; uint32_t seed = 1;
  for (;;) {
    rng_state = seed++ * 2654435761u | 1; int tok = 1, prev, n = 0;
    uint64_t t0 = rdcycle();
    for (int pos = 0; pos < 200; pos++) {
      forward(&m, tok, pos); prev = tok; tok = sample_logits(0.8f);
      if (tok == 1) break;
      decode_token(&m, prev, tok, emit); n++;
    }
    uint64_t c = rdcycle() - t0;
    uart_puts("\r\n[llm] "); put_dec(n); uart_puts(" tokens, "); put_fixed2((uint64_t)n * CPU_HZ * 100 / c);
    uart_puts(" tok/s\r\n\r\n");
  }
}
