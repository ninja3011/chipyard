#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
extern const uint8_t model_blob[];
static void hex32(uint32_t v){const char*h="0123456789abcdef";for(int i=28;i>=0;i-=4)uart_putc(h[(v>>i)&15]);}
int main(void){
  uart_init(); uart_puts("\r\n[chunks] start\r\n");
  const uint32_t *w=(const uint32_t*)model_blob; const uint32_t n=65536, C=1024;
  for(uint32_t c=0;c*C<n;c++){ uint32_t s=0,e=(c+1)*C<n?(c+1)*C:n; for(uint32_t i=c*C;i<e;i++) s+=w[i];
    hex32(c); uart_putc(':'); hex32(s); uart_puts("\r\n"); }
  uart_puts("[chunks] done "); hex32((uint32_t)(uintptr_t)model_blob); uart_puts("\r\n"); for(;;){}
}
