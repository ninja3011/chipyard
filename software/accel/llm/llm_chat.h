// Interactive chat over a serial console: type a prompt, the model continues it;
// "/train <text>" fine-tunes the LoRA adapter on that text, on the chip.
// Platform-independent: the wrapper provides chat_putc / chat_getc / chat_ticks
// (UART on the board, stdin/stdout on the PC for debugging the logic).
// Requires MODEL_15M (or default 260K) and optionally USE_ACCEL before including.
#ifndef LLM_CHAT_H
#define LLM_CHAT_H
#include "llm_core.h"

extern void chat_putc(char c);
extern int chat_getc(void);            /* blocking; returns 0..255, or -1 on EOF */
extern uint64_t chat_ticks(void);      /* free-running counter */
#ifndef CHAT_HZ
#define CHAT_HZ 0                      /* ticks per second (0 = don't print speed) */
#endif

static void chat_puts(const char *s) { while (*s) { if (*s == '\n') chat_putc('\r'); chat_putc(*s++); } }
static void chat_emit(const char *s, int n) { for (int i = 0; i < n; i++) { if (s[i] == '\n') chat_putc('\r'); chat_putc(s[i]); } }
static void chat_dec(uint64_t v) {
  char b[24]; int n = 0; if (!v) b[n++] = '0'; while (v) { b[n++] = '0' + v % 10; v /= 10; } while (n) chat_putc(b[--n]);
}
static void chat_fixed(int64_t x, int decimals) {   /* x scaled by 10^decimals */
  if (x < 0) { chat_putc('-'); x = -x; }
  int64_t p = 1; for (int i = 0; i < decimals; i++) p *= 10;
  chat_dec(x / p); chat_putc('.');
  for (int64_t d = p / 10; d >= 1; d /= 10) chat_putc('0' + (x / d) % 10);
}

/* ---- on-chip BPE tokenizer (llama2.c algorithm) ------------------------------ */
#define HT_SIZE 65536
static uint16_t ht_[HT_SIZE];                 /* token id + 1, 0 = empty slot */
static const float *tok_scores_;
static const Model *tm_;
static uint32_t hash_bytes(const char *p, int n) { uint32_t h = 2166136261u; for (int i = 0; i < n; i++) h = (h ^ (uint8_t)p[i]) * 16777619u; return h; }
static int tok_len(int id) { return (int)(tm_->tok_off[id + 1] - tm_->tok_off[id]); }
static const char *tok_str(int id) { return tm_->tok_data + tm_->tok_off[id]; }
static int tok_lookup(const char *s, int n) {
  uint32_t h = hash_bytes(s, n) & (HT_SIZE - 1);
  while (ht_[h]) {
    int id = ht_[h] - 1;
    if (tok_len(id) == n) { const char *t = tok_str(id); int i = 0; while (i < n && t[i] == s[i]) i++; if (i == n) return id; }
    h = (h + 1) & (HT_SIZE - 1);
  }
  return -1;
}
static void tok_init(const Model *m, const uint8_t *blob) {
  tm_ = m; tok_scores_ = (const float *)sec(blob, SEC_TOK_SCORES);
  for (int i = 0; i < HT_SIZE; i++) ht_[i] = 0;
  for (int id = 0; id < VOCAB; id++) {
    if (tok_lookup(tok_str(id), tok_len(id)) >= 0) continue;   /* keep first of any duplicate string */
    uint32_t h = hash_bytes(tok_str(id), tok_len(id)) & (HT_SIZE - 1);
    while (ht_[h]) h = (h + 1) & (HT_SIZE - 1);
    ht_[h] = (uint16_t)(id + 1);
  }
}
/* text -> ids, BOS first. Returns count. */
static int tok_encode(const char *text, int tlen, int *ids, int maxids) {
  int n = 0; ids[n++] = 1;
  if (tlen > 0) { int sp = tok_lookup(" ", 1); if (sp >= 0) ids[n++] = sp; }
  for (int i = 0; i < tlen && n < maxids; i++) {
    int id = tok_lookup(&text[i], 1);
    ids[n++] = id >= 0 ? id : (uint8_t)text[i] + 3;         /* byte-fallback tokens <0xXX> */
  }
  for (;;) {
    float best = -1e10f; int bi = -1, bid = -1; char buf[128];
    for (int i = 0; i + 1 < n; i++) {
      int a = ids[i], b = ids[i + 1], la = tok_len(a), lb = tok_len(b);
      if (la + lb >= 128) continue;
      for (int k = 0; k < la; k++) buf[k] = tok_str(a)[k];
      for (int k = 0; k < lb; k++) buf[la + k] = tok_str(b)[k];
      int id = tok_lookup(buf, la + lb);
      if (id >= 0 && tok_scores_[id] > best) { best = tok_scores_[id]; bi = i; bid = id; }
    }
    if (bi < 0) break;
    ids[bi] = bid; for (int i = bi + 1; i + 1 < n; i++) ids[i] = ids[i + 1]; n--;
  }
  return n;
}

/* ---- console ------------------------------------------------------------------ */
static int chat_readline(char *buf, int max) {
  int n = 0;
  for (;;) {
    int c = chat_getc();
    if (c < 0) return -1;
    if (c == '\r' || c == '\n') { chat_puts("\n"); buf[n] = 0; return n; }
    if (c == 8 || c == 127) { if (n > 0) { n--; chat_puts("\b \b"); } continue; }
    if (c >= 32 && c < 127 && n < max - 1) { buf[n++] = (char)c; chat_putc((char)c); }
  }
}

static float g_temp = 0.8f;
static int g_maxnew = 40;
static int g_lora_ready = 0;

static void chat_generate(const Model *m, const char *prompt, int plen) {
  static int ids[MAXPOS];
  int n = tok_encode(prompt, plen, ids, MAXPOS - 8);
  uint64_t t0 = chat_ticks();
  chat_emit(prompt, plen);
  int pos = 0;
  for (int i = 0; i < n; i++) forward(m, ids[i], pos++);
  int prev = ids[n - 1], gen = 0;
  rng_state = (uint32_t)(chat_ticks() * 2654435761u) | 1;
  while (pos < MAXPOS - 1 && gen < g_maxnew) {
    int tok = sample_logits(g_temp);
    if (tok == 1 || tok == 2) break;
    decode_token(m, prev, tok, chat_emit);
    prev = tok; gen++;
    forward(m, tok, pos++);
  }
  chat_puts("\n");
  if (CHAT_HZ) {
    uint64_t dt = chat_ticks() - t0;
    chat_puts("["); chat_dec(n); chat_puts(" prompt + "); chat_dec(gen); chat_puts(" new tokens, ");
    chat_fixed((int64_t)((uint64_t)(n + gen) * CHAT_HZ * 100 / (dt ? dt : 1)), 2); chat_puts(" tok/s]\n");
  }
}

static void chat_train(const Model *m, const char *text, int tlen, int epochs) {
  static int ids[MAXPOS];
  int n = tok_encode(text, tlen, ids, MAXPOS - 1);
  if (n < 4) { chat_puts("give me a longer text to learn\n"); return; }
  if (!g_lora_ready) { rng_state = 12345; lora_init(); g_lora_ready = 1; }
  g_lora_on = 1;
  chat_puts("learning "); chat_dec(n); chat_puts(" tokens, "); chat_dec(epochs); chat_puts(" epochs\n");
  for (int e = 0; e < epochs; e++) {
    float lre = 0.004f * (1.0f - 0.9f * (float)e / (float)epochs);
    uint64_t t0 = chat_ticks();
    float loss = lora_epoch(m, ids, n, lre);
    chat_puts("epoch "); chat_dec(e + 1); chat_puts("  loss "); chat_fixed((int64_t)(loss * 10000.f), 4);
    if (CHAT_HZ) { chat_puts("  ("); chat_fixed((int64_t)((chat_ticks() - t0) * 10 / CHAT_HZ), 1); chat_puts(" s)"); }
    chat_puts("\n");
  }
  chat_puts("done. the model now uses what it learned (/base to switch it off)\n");
}

static int chat_atoi(const char *s, int *adv) { int v = 0, i = 0; while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; } *adv = i; return v; }
static int chat_starts(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }

static void chat_help(void) {
  chat_puts("type a prompt and press Enter -> the model continues it\n"
            "/train [epochs] <text>  learn <text> on-chip (LoRA), then use it\n"
            "/base  /tuned           switch the learned adapter off / on\n"
            "/reset                  forget everything it learned\n"
            "/temp <0-9>             randomness (0 = greedy, 8 = default 0.8)\n"
            "/len <n>                max new tokens (default 40)\n");
}

static void chat_main(const uint8_t *blob) {
  static Model model; model_init(&model, blob); tok_init(&model, blob);
  chat_puts("\n[chat] TinyStories LLM on my own RISC-V chip + INT8 matrix engine\n");
  chat_help();
  static char line[200];
  for (;;) {
    chat_puts("\n> ");
    int n = chat_readline(line, sizeof line);
    if (n < 0) return;
    if (n == 0) continue;
    if (line[0] != '/') { chat_generate(&model, line, n); continue; }
    if (chat_starts(line, "/train")) {
      const char *p = line + 6; while (*p == ' ') p++;
      int adv = 0, ep = chat_atoi(p, &adv);
      if (adv > 0 && p[adv] == ' ') { p += adv; while (*p == ' ') p++; } else ep = 10;
      if (ep < 1) ep = 1; if (ep > 60) ep = 60;
      int tl = 0; while (p[tl]) tl++;
      chat_train(&model, p, tl, ep);
    } else if (chat_starts(line, "/base")) { g_lora_on = 0; chat_puts("base model\n"); }
    else if (chat_starts(line, "/tuned")) { if (g_lora_ready) { g_lora_on = 1; chat_puts("using what it learned\n"); } else chat_puts("nothing learned yet\n"); }
    else if (chat_starts(line, "/reset")) { rng_state = 12345; lora_init(); g_lora_on = 0; g_lora_ready = 1; chat_puts("forgotten\n"); }
    else if (chat_starts(line, "/temp")) { int adv; int v = chat_atoi(line + 6, &adv); g_temp = (float)v / 10.f; chat_puts("ok\n"); }
    else if (chat_starts(line, "/len")) { int adv; int v = chat_atoi(line + 5, &adv); if (v > 0 && v < MAXPOS - 20) g_maxnew = v; chat_puts("ok\n"); }
    else chat_help();
  }
}
#endif
