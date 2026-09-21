#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define USE_ACCEL
#include "llm_core.h"
extern const uint8_t model_blob[];
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }
static void put_dec(uint64_t v) { char b[24]; int n = 0; if (!v) b[n++]='0'; while (v) { b[n++]='0'+v%10; v/=10; } while (n) uart_putc(b[--n]); }
int main(void) {
  uart_init(); uart_puts("\r\n[t] start\r\n");
  Model m; model_init(&m, model_blob);
  uint64_t t0 = rdcycle(); g_use_accel = 1; forward(&m, 1, 0); uint64_t t1 = rdcycle();
  g_use_accel = 0; forward(&m, 1, 0); uint64_t t2 = rdcycle();
  uart_puts("[t] engine cyc "); put_dec(t1 - t0); uart_puts("  software cyc "); put_dec(t2 - t1); uart_puts("\r\n");
  static float ref[VOCAB]; g_use_accel = 1; forward(&m, 1, 0); for (int i = 0; i < VOCAB; i++) ref[i] = logits_[i];
  g_use_accel = 0; forward(&m, 1, 0); int d = 0; for (int i = 0; i < VOCAB; i++) if (ref[i] != logits_[i]) d++;
  uart_puts("[t] mismatches "); put_dec(d); uart_puts("\r\n");
  for (;;) {}
}
