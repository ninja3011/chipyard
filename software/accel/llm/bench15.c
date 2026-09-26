// Benchmark: TinyStories-15M generation, plain CPU vs the INT8 tile engine.
// Same model, same tokens (greedy from BOS); reports cycles per token, tokens/s,
// how many of those cycles are inside the matvecs, and one LoRA training step.
// Run once per bitstream (broadcast engine, systolic engine); the CPU-only
// numbers must match between them.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define MODEL_15M
#define USE_ACCEL
#define BENCH_COUNT
#include "llm_core.h"
extern const uint8_t model_blob[];
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }
static void put_dec(uint64_t v) { char b[24]; int n = 0; if (!v) b[n++] = '0'; while (v) { b[n++] = '0' + v % 10; v /= 10; } while (n) uart_putc(b[--n]); }
static void put_fixed(int64_t x, int d) { if (x < 0) { uart_putc('-'); x = -x; } int64_t p = 1; for (int i = 0; i < d; i++) p *= 10; put_dec(x / p); uart_putc('.'); for (int64_t q = p / 10; q >= 1; q /= 10) uart_putc('0' + (x / q) % 10); }
static void kv(const char *k, uint64_t v) { uart_puts(k); uart_putc('='); put_dec(v); uart_putc(' '); }
#define WEIGHT_BYTES 15757648ULL

int main(void) {
  uart_init(); uart_puts("\r\n[bench] TinyStories-15M generation: CPU-only vs INT8 tile engine\r\n");
  Model m; model_init(&m, model_blob);
  static uint64_t tok_cyc[2][8], tok_mv[2][8]; int NT[2] = {4, 8};
  int tokens_seen[2][8];
  for (int mode = 0; mode < 2; mode++) {            /* 0 = plain CPU, 1 = engine */
    g_use_accel = mode; int tok = 1;
    for (int pos = 0; pos < NT[mode]; pos++) {
      g_mv_cyc = 0; uint64_t t0 = rdcycle(); forward(&m, tok, pos); uint64_t t1 = rdcycle();
      tok_cyc[mode][pos] = t1 - t0; tok_mv[mode][pos] = g_mv_cyc;
      tok = argmax_logits(); tokens_seen[mode][pos] = tok;
    }
  }
  int same = 1; for (int i = 0; i < 4; i++) if (tokens_seen[0][i] != tokens_seen[1][i]) same = 0;
  for (int mode = 0; mode < 2; mode++) {
    uint64_t c = 0, mvc = 0; int n = NT[mode];
    for (int i = 0; i < n; i++) { c += tok_cyc[mode][i]; mvc += tok_mv[mode][i]; }
    uint64_t per = c / n;
    uart_puts(mode ? "engine " : "cpu    "); kv("tokens", n); kv("cyc_per_token", per); kv("matvec_pct", mvc * 100 / c);
    uart_puts("tok_per_s="); put_fixed((int64_t)(50000000ULL * 100 / per), 2);
    uart_puts(" weights_MB_per_s="); put_fixed((int64_t)(WEIGHT_BYTES * 500ULL / per), 1);
    uart_puts("\r\n");
  }
  uint64_t cpu = 0, eng = 0; for (int i = 0; i < NT[0]; i++) cpu += tok_cyc[0][i]; cpu /= NT[0];
  for (int i = 0; i < NT[1]; i++) eng += tok_cyc[1][i]; eng /= NT[1];
  uart_puts("[bench] generation speedup engine vs cpu (x100): "); put_dec(cpu * 100 / eng); uart_puts("\r\n");
  uart_puts("[bench] same first 4 tokens in both modes: "); uart_puts(same ? "yes" : "NO"); uart_puts("\r\n");
  /* one LoRA training step (forward + backward), lr=0 so nothing changes */
  rng_state = 1234; lora_init(); uint64_t ts[2];
  for (int mode = 0; mode < 2; mode++) {
    g_use_accel = mode; uint64_t t0 = rdcycle(); forward(&m, 1, 0); lora_step(&m, 100, 0.f); ts[mode] = rdcycle() - t0;
  }
  uart_puts("train_step cpu_cyc="); put_dec(ts[0]); uart_puts(" engine_cyc="); put_dec(ts[1]);
  uart_puts(" speedup_x100="); put_dec(ts[0] * 100 / ts[1]); uart_puts("\r\n[bench] DONE\r\n");
  for (;;) {}
}
