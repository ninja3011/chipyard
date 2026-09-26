// SmolLM2-135M-Instruct on Rocket + the INT8 tile engine. The chip does the transformer; the PC does the
// (byte-level BPE) tokenizing and decoding, so the serial protocol is token ids:
//   PC -> chip:  "R\n"                         reset the conversation (position 0)
//                "G <maxnew> <temp*10> id id id ...\n"  append these ids, then generate
//   chip -> PC:  "READY\n>"  after boot;  per generated token "<id> ";  then "\nEND <tokens> <cycles>\n>"
// KV cache persists between G commands, so a chat only pays for the new tokens each turn.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define MODEL_SMOL
#define USE_ACCEL
#include "llm_core.h"
extern const uint8_t model_blob[];
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }
static void put_dec(uint64_t v) { char b[24]; int n = 0; if (!v) b[n++] = '0'; while (v) { b[n++] = '0' + v % 10; v /= 10; } while (n) uart_putc(b[--n]); }
static void puts_(const char *s) { while (*s) { if (*s == '\n') uart_putc('\r'); uart_putc(*s++); } }
static int getc_(void) { for (;;) { uint32_t v = uart_reg_read(UART_REG_RXFIFO); if (!(v & UART_RXFIFO_EMPTY)) return (int)(v & 0xff); } }
static int readline(char *buf, int max) {   /* no echo: the PC client shows what the user typed */
  int n = 0; for (;;) { int c = getc_(); if (c == '\r' || c == '\n') { if (n) { buf[n] = 0; return n; } continue; } if (n < max - 1) buf[n++] = (char)c; }
}
static int parse_int(const char **p) { while (**p == ' ') (*p)++; int v = 0; while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; } return v; }

int main(void) {
  uart_init(); g_use_accel = 1; static Model m; model_init(&m, model_blob);
  puts_("\n[smol] SmolLM2-135M-Instruct: 30 layers, 135M params, INT8 on the tile engine\nREADY\n>");
  static char line[4096]; int pos = 0;
  for (;;) {
    int n = readline(line, sizeof line); (void)n;
    if (line[0] == 'R') { pos = 0; puts_("\nOK reset\n>"); continue; }
    if (line[0] != 'G') { puts_("\nERR\n>"); continue; }
    const char *p = line + 1; int maxnew = parse_int(&p); int temp10 = parse_int(&p);
    uint64_t t0 = rdcycle(); int ntok = 0, last = -1;
    for (;;) { while (*p == ' ') p++; if (*p < '0' || *p > '9') break; int id = parse_int(&p); if (pos >= MAXPOS - 1) break; forward(&m, id, pos++); last = id; }
    if (last < 0) { puts_("\nERR noids\n>"); continue; }
    rng_state = (uint32_t)(rdcycle() * 2654435761u) | 1; int full = 0;
    for (int g = 0; g < maxnew; g++) {
      int tok = sample_logits(temp10 / 10.0f);
      put_dec((uint64_t)tok); uart_putc(' ');
      ntok++;
      if (pos >= MAXPOS - 1) { full = 1; break; }
      forward(&m, tok, pos++);            /* feed the sampled token (including the end token) so the cache stays consistent */
      if (tok == 2 || tok == 0) break;
    }
    puts_("\nEND "); put_dec((uint64_t)ntok); uart_putc(' '); put_dec(rdcycle() - t0); puts_(full ? " FULL\n>" : "\n>");
  }
}
