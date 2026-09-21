asm(".section .text.startup,\"ax\"\n.skip 8192\n.text\n");
// On-chip LoRA fine-tuning of the INT8 TinyStories-260K model on Rocket + the
// 8x8 tile engine. Forward passes and the classifier-transpose gradient run on
// the engine; norms/attention/softmax/adapter update run on the core in float.
// Shows the same prompt (BOS) before and after fine-tuning, live on the UART.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define MODEL_15M
#define USE_ACCEL
#include "llm_core.h"
#include "corpus15b.h"

extern const uint8_t model_blob[];
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }
static void put_dec(uint64_t v) {
  char b[24]; int n = 0; if (!v) b[n++] = '0'; while (v) { b[n++] = '0' + v % 10; v /= 10; } while (n) uart_putc(b[--n]);
}
static void put_fixed(int64_t x, int decimals) {  // x scaled by 10^decimals
  if (x < 0) { uart_putc('-'); x = -x; }
  int64_t p = 1; for (int i = 0; i < decimals; i++) p *= 10;
  put_dec(x / p); uart_putc('.');
  for (int64_t d = p / 10; d >= 1; d /= 10) uart_putc('0' + (x / d) % 10);
}
static void emit(const char *s, int n) {
  for (int i = 0; i < n; i++) { if (s[i] == '\n') uart_putc('\r'); uart_putc(s[i]); }
}
#define CPU_HZ 50000000ULL

static void generate(const Model *m, const char *label, int steps) {
  uart_puts(label);
  int tok = 1, prev;
  for (int pos = 0; pos < steps; pos++) {
    forward(m, tok, pos); prev = tok; tok = argmax_logits(); if (tok == 1 || tok == 2) break;
    decode_token(m, prev, tok, emit);
  }
  uart_puts("\r\n\r\n");
}


// Trap reporter: prints why the core trapped instead of spinning silently.
static void put_hex(uint64_t v){ const char*h="0123456789abcdef"; for(int i=60;i>=0;i-=4) uart_putc(h[(v>>i)&15]); }
__attribute__((interrupt("machine"))) void trap_handler(void) {
  uint64_t c,e,t; asm volatile("csrr %0,mcause":"=r"(c)); asm volatile("csrr %0,mepc":"=r"(e)); asm volatile("csrr %0,mtval":"=r"(t));
  uart_puts("\r\nTRAP mcause="); put_hex(c); uart_puts(" mepc="); put_hex(e); uart_puts(" mtval="); put_hex(t); uart_puts("\r\n");
  for(;;){}
}
int main(void) {
  asm volatile("csrw mtvec,%0"::"r"(trap_handler));
  uart_init();
  uart_puts("\r\n[lora] on-chip LoRA fine-tuning: TinyStories-15M INT8 on 8x8 tile engine, rank-8 LoRA on last-layer w2\r\n");
  Model m; model_init(&m, model_blob);

  static float ref_logits[VOCAB];
  g_use_accel = 1; forward(&m, 1, 0);
  for (int i = 0; i < VOCAB; i++) ref_logits[i] = logits_[i];
  g_use_accel = 0; forward(&m, 1, 0);
  int diff = 0; for (int i = 0; i < VOCAB; i++) if (ref_logits[i] != logits_[i]) diff++;
  uart_puts(diff ? "[lora] forward engine vs software: MISMATCH\r\n" : "[lora] forward engine vs software: bit-identical\r\n");

  // speed of one training step (forward + backward, lr=0 so nothing changes)
  rng_state = 12345; lora_init(); uint64_t cyc[2];
  for (int b = 0; b < 2; b++) {
    g_use_accel = b; uint64_t t0 = rdcycle();
    for (int i = 0; i < 3; i++) { forward(&m, corpus[i], i); lora_step(&m, corpus[i + 1], 0.f); }
    cyc[b] = rdcycle() - t0;
  }
  uart_puts("[lora] 3 training steps: software "); put_dec(cyc[0] / 1000); uart_puts(" kcyc, engine ");
  put_dec(cyc[1] / 1000); uart_puts(" kcyc -> "); put_fixed(cyc[0] * 100 / cyc[1], 2); uart_puts("x faster\r\n\r\n");

  g_use_accel = 1; g_lora_on = 0;
  generate(&m, "--- base model (before fine-tuning) ---\r\n", 60);

  rng_state = 12345; lora_init();
  uart_puts("--- fine-tuning (Adam) on "); put_dec(CORPUS_N); uart_puts(" tokens ---\r\n");
  for (int e = 0; e < 20; e++) {
    float lre = 0.004f * (1.0f - 0.9f * (float)e / 20.0f);
    uint64_t t0 = rdcycle(); float loss = lora_epoch(&m, corpus, CORPUS_N, lre);
    uint64_t c = rdcycle() - t0;
    uart_puts("epoch "); put_dec(e + 1); uart_puts("  loss "); put_fixed((int64_t)(loss * 10000.f), 4);
    uart_puts("  ("); put_fixed((int64_t)(c * 100 / CPU_HZ), 2); uart_puts(" s)\r\n");
  }
  uart_puts("\r\n");
  for (;;) {
    g_lora_on = 0; generate(&m, "--- base model ---\r\n", 60);
    g_lora_on = 1; generate(&m, "--- after LoRA fine-tuning ---\r\n", 60);
  }
}
