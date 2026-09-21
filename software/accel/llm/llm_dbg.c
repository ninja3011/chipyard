#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define DBG(c) uart_putc(c)
#define USE_ACCEL
#include "llm_core.h"
extern const uint8_t model_blob[];
int main(void) {
  uart_init(); uart_puts("\r\n[dbg] start\r\n");
  Model m; model_init(&m, model_blob); uart_puts("[dbg] model ok\r\n");
  g_use_accel = 1; forward(&m, 1, 0);
  uart_puts("\r\n[dbg] forward done\r\n");
  for (;;) {}
}
