// Engine LLM stress: software reference once, then repeat the engine forward
// pass and compare all 512 logits bit-for-bit every iteration.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define USE_ACCEL
#include "llm_core_mis.h"
extern const uint8_t model_blob[];
static void put_dec(uint64_t v){char b[24];int n=0;if(!v)b[n++]='0';while(v){b[n++]='0'+v%10;v/=10;}while(n)uart_putc(b[--n]);}
int main(void) {
  uart_init(); uart_puts("\r\n[llm-stress] start\r\n");
  Model m; model_init(&m, model_blob);
  static float ref[VOCAB]; g_use_accel = 0;
  forward(&m, 1, 0); for (int i = 0; i < VOCAB; i++) ref[i] = logits_[i];
  g_use_accel = 1;
  for (uint32_t it = 1;; it++) {
    forward(&m, 1, 0); int d = 0; for (int i = 0; i < VOCAB; i++) if (ref[i] != logits_[i]) d++;
    if (d) { uart_puts("\r\n[llm-stress] MISMATCH it="); put_dec(it); uart_puts(" n="); put_dec(d); uart_puts("\r\n"); }
    else uart_putc('.');
    if (it % 40 == 0) { uart_puts(" it="); put_dec(it); uart_puts("\r\n"); }
  }
}
