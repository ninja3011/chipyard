// SmolLM2-135M-Instruct on Rocket + the INT8 tile engine. The chip does the transformer; the PC does the
// (byte-level BPE) tokenizing and decoding, so the serial protocol is token ids:
//   PC -> chip:  "R\n"                         reset the conversation (position 0)
//                "G <maxnew> <temp*10> <rep*100> id id id ...\n"  append these ids, then generate (rep>100 = repetition penalty)
//                "T <epochs> id id id ...\n"    LoRA-train on this token sequence (a full chat turn: prompt + reply),
//                                                 teacher-forced, on the chip. Frozen 135M base untouched; only a
//                                                 ~17K-parameter adapter on the last layer's FFN down-projection
//                                                 (see llm_core.h) is trained. First T call initializes the adapter;
//                                                 later T calls keep training the same one. Clobbers the KV cache
//                                                 (send R afterwards). Prompt processing in G is batched (8 tokens/
//                                                 tile) and now applies the trained adapter to the final position too.
//                "O\n"  /  "F\n"                  adapter off (base model) / on (tuned model) for subsequent G's
//   chip -> PC:  "READY\n>"  after boot;  per generated token "<id> ";  then "\nEND <tokens> <cycles>\n>"
//                per T epoch: "EPOCH <n> <loss_x10000> <cycles>\n";  then "\nTRAINDONE\n>"
// KV cache persists between G commands, so a chat only pays for the new tokens each turn.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define MODEL_SMOL
#define USE_ACCEL
#include "llm_core.h"
#include "llm_batch.h"
extern const uint8_t model_blob[];
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }
static void put_dec(uint64_t v) { char b[24]; int n = 0; if (!v) b[n++] = '0'; while (v) { b[n++] = '0' + v % 10; v /= 10; } while (n) uart_putc(b[--n]); }
static void puts_(const char *s) { while (*s) { if (*s == '\n') uart_putc('\r'); uart_putc(*s++); } }
static int getc_(void) { for (;;) { uint32_t v = uart_reg_read(UART_REG_RXFIFO); if (!(v & UART_RXFIFO_EMPTY)) return (int)(v & 0xff); } }
static int readline(char *buf, int max) {   /* no echo: the PC client shows what the user typed */
  int n = 0; for (;;) { int c = getc_(); if (c == '\r' || c == '\n') { if (n) { buf[n] = 0; return n; } continue; } if (n < max - 1) buf[n++] = (char)c; }
}
static int parse_int(const char **p) { while (**p == ' ') (*p)++; int v = 0; while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; } return v; }
static void put_fixed(int64_t x, int decimals) {  /* x scaled by 10^decimals */
  if (x < 0) { uart_putc('-'); x = -x; }
  int64_t p10 = 1; for (int i = 0; i < decimals; i++) p10 *= 10;
  put_dec(x / p10); uart_putc('.');
  for (int64_t d = p10 / 10; d >= 1; d /= 10) uart_putc('0' + (x / d) % 10);
}

static int g_lora_ready = 0;
int main(void) {
  uart_init(); g_use_accel = 1; static Model m; model_init(&m, model_blob);
  puts_("\n[smol] SmolLM2-135M-Instruct: 30 layers, 135M params, INT8 on the tile engine\nREADY\n>");
  static char line[4096]; int pos = 0;
  for (;;) {
    int n = readline(line, sizeof line); (void)n;
    if (line[0] == 'R') { pos = 0; puts_("\nOK reset\n>"); continue; }
    if (line[0] == 'C') {   /* self-check + benchmark: engine vs plain CPU on the same forward passes (clobbers the KV cache: send R afterwards) */
      static float ref[VOCAB]; uint64_t te = 0, tc = 0; int diff = 0; float maxd = 0.f;
      for (int i = 0; i < 2; i++) {
        g_use_accel = 1; uint64_t t0 = rdcycle(); forward(&m, 1000 + i, i); te += rdcycle() - t0;
        for (int k = 0; k < VOCAB; k++) ref[k] = logits_[k];
        g_use_accel = 0; t0 = rdcycle(); forward(&m, 1000 + i, i); tc += rdcycle() - t0;
        for (int k = 0; k < VOCAB; k++) { if (ref[k] != logits_[k]) diff++; float d = ref[k] - logits_[k]; if (d < 0) d = -d; if (d > maxd) maxd = d; }
      }
      g_use_accel = 1; pos = 0;
      puts_("\nCHECK forwards=2 logits_compared="); put_dec(2ULL * VOCAB); puts_(" mismatches="); put_dec(diff);
      puts_(" engine_cyc_per_forward="); put_dec(te / 2); puts_(" cpu_cyc_per_forward="); put_dec(tc / 2);
      puts_(" speedup_x100="); put_dec(tc * 100 / (te ? te : 1)); puts_("\n>"); continue;
    }
    if (line[0] == 'B') {   /* batched-vs-sequential prefill check (16 tokens); send R afterwards */
      static float ref2[VOCAB]; static int tk16[16]; for (int i = 0; i < 16; i++) tk16[i] = 1000 + 7 * i; int diff = 0;
      uint64_t t0 = rdcycle(); for (int i = 0; i < 16; i++) forward(&m, tk16[i], i); uint64_t seq = rdcycle() - t0;
      for (int k = 0; k < VOCAB; k++) ref2[k] = logits_[k];
      t0 = rdcycle(); for (int q = 0; q < 16; q += NB) forward_batch_ex(&m, tk16 + q, NB, q, q + NB >= 16); uint64_t bat = rdcycle() - t0;
      for (int k = 0; k < VOCAB; k++) if (ref2[k] != logits_[k]) diff++;
      pos = 0; puts_("\nBATCH tokens=16 logits_compared="); put_dec(VOCAB); puts_(" mismatches="); put_dec(diff);
      puts_(" seq_cyc="); put_dec(seq); puts_(" batch_cyc="); put_dec(bat); puts_(" speedup_x100="); put_dec(seq * 100 / (bat ? bat : 1)); puts_("\n>"); continue;
    }
    if (line[0] == 'O') { g_lora_on = 0; puts_("\nOK base\n>"); continue; }
    if (line[0] == 'F') {
      if (!g_lora_ready) { puts_("\nERR untrained\n>"); continue; }
      g_lora_on = 1; puts_("\nOK tuned\n>"); continue;
    }
    if (line[0] == 'T') {   /* LoRA train: "T <epochs> id id id ..." -- clobbers pos/KV cache, send R after */
      const char *tp = line + 1; int epochs = parse_int(&tp);
      if (epochs < 1) epochs = 1; if (epochs > 60) epochs = 60;
      static int tids[MAXPOS]; int tn = 0;
      for (;;) { while (*tp == ' ') tp++; if (*tp < '0' || *tp > '9') break; if (tn < MAXPOS - 1) tids[tn++] = parse_int(&tp); else parse_int(&tp); }
      if (tn < 4) { puts_("\nERR needmoretokens\n>"); continue; }
      if (!g_lora_ready) { rng_state = 12345; lora_init(); g_lora_ready = 1; }
      g_lora_on = 1; pos = 0;
      for (int e = 0; e < epochs; e++) {
        float lre = 0.004f * (1.0f - 0.9f * (float)e / (float)epochs);
        uint64_t te0 = rdcycle(); float loss = lora_epoch(&m, tids, tn, lre); uint64_t tec = rdcycle() - te0;
        puts_("\nEPOCH "); put_dec((uint64_t)(e + 1)); uart_putc(' '); put_fixed((int64_t)(loss * 10000.f), 4); uart_putc(' '); put_dec(tec);
      }
      pos = 0;
      puts_("\nTRAINDONE\n>"); continue;
    }
    if (line[0] != 'G') { puts_("\nERR\n>"); continue; }
    const char *p = line + 1; int maxnew = parse_int(&p); int temp10 = parse_int(&p); int rep100 = parse_int(&p);
    static int hist[128]; int nh = 0;
    uint64_t t0 = rdcycle(); int ntok = 0, last = -1;
    static int pids[512]; int cnt = 0;
    for (;;) { while (*p == ' ') p++; if (*p < '0' || *p > '9') break; int id = parse_int(&p); if (cnt < 512 && pos + cnt < MAXPOS - 1) pids[cnt++] = id; }
    if (cnt == 0) { puts_("\nERR noids\n>"); continue; }
    for (int q = 0; q < cnt; q += NB) {      /* prompt: 8 tokens at a time (fills the engine's 8 columns) */
      int nb = cnt - q < NB ? cnt - q : NB; forward_batch_ex(&m, pids + q, nb, pos, (q + nb >= cnt)); pos += nb;
    }
    last = pids[cnt - 1];
    rng_state = (uint32_t)(rdcycle() * 2654435761u) | 1; int full = 0;
    for (int g = 0; g < maxnew; g++) {
      if (rep100 > 100) for (int h = 0; h < nh; h++) { float *l = &logits_[hist[h]]; float r = rep100 / 100.0f; *l = (*l > 0.f) ? *l / r : *l * r; }
      int tok = sample_logits(temp10 / 10.0f); if (nh < 128) hist[nh++] = tok;
      put_dec((uint64_t)tok); uart_putc(' ');
      ntok++;
      if (pos >= MAXPOS - 1) { full = 1; break; }
      forward(&m, tok, pos++);            /* feed the sampled token (including the end token) so the cache stays consistent */
      if (tok == 2 || tok == 0) break;
    }
    puts_("\nEND "); put_dec((uint64_t)ntok); uart_putc(' '); put_dec(rdcycle() - t0); puts_(full ? " FULL\n>" : "\n>");
  }
}
