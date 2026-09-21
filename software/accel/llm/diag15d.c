#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
extern const uint8_t model_blob[];
static void hex32(uint32_t v){const char*h="0123456789abcdef";for(int i=28;i>=0;i-=4)uart_putc(h[(v>>i)&15]);}
int main(void){
  uart_init(); uart_puts("\r\n[words] start "); hex32((uint32_t)(uintptr_t)model_blob); uart_puts("\r\n");
  const uint32_t *w=(const uint32_t*)model_blob;
  for(uint32_t i=0;i<1024;i++){ if(w[i]){ hex32(i); uart_putc(':'); hex32(w[i]); uart_puts("\r\n"); } }
  uart_puts("[words] done\r\n"); for(;;){}
}
