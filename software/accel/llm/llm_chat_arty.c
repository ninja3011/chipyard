// Board wrapper for llm_chat.h: prompts in and text out over the FT232R console
// (UART0, 115200 8N1); matvecs run on the INT8 tile engine.
#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define MODEL_15M
#define USE_ACCEL
#define CHAT_HZ 50000000ULL
#include "llm_chat.h"
extern const uint8_t model_blob[];
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile("rdcycle %0" : "=r"(c)); return c; }
void chat_putc(char c) { uart_putc(c); }
int chat_getc(void) {
  for (;;) { uint32_t v = uart_reg_read(UART_REG_RXFIFO); if (!(v & UART_RXFIFO_EMPTY)) return (int)(v & 0xff); }
}
uint64_t chat_ticks(void) { return rdcycle(); }
int main(void) { uart_init(); g_use_accel = 1; chat_main(model_blob); for (;;) {} }
