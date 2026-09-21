#include <stdint.h>
#include "../../doom/baremetal-arty100t/uart.h"
#define MODEL_15M
#define USE_ACCEL
#include "llm_core.h"
extern const uint8_t model_blob[];
static void hex32(uint32_t v){const char*h="0123456789abcdef";for(int i=28;i>=0;i-=4)uart_putc(h[(v>>i)&15]);}
static void hexf(float f){union{float f;uint32_t u;}x;x.f=f;hex32(x.u);uart_putc(' ');}
int main(void){
  uart_init(); uart_puts("\r\n[diag15] start\r\n");
  const uint32_t *w=(const uint32_t*)model_blob; uint32_t s=0,x=0; const uint32_t n=15757648/4;
  for(uint32_t i=0;i<n;i++){s+=w[i];x^=w[i];}
  uart_puts("blob sum32="); hex32(s); uart_puts(" xor="); hex32(x); uart_puts("\r\n");
  Model m; model_init(&m,model_blob);
  g_use_accel=1; forward(&m,1,0); uart_puts("engine logits[0..3]: "); for(int i=0;i<4;i++)hexf(logits_[i]);
  uart_puts("\r\nx_[0..2]: "); for(int i=0;i<3;i++)hexf(x_[i]);
  g_use_accel=0; forward(&m,1,0); uart_puts("\r\nsoft logits[0..3]: "); for(int i=0;i<4;i++)hexf(logits_[i]);
  uart_puts("\r\nx_[0..2]: "); for(int i=0;i<3;i++)hexf(x_[i]);
  uart_puts("\r\n[diag15] done\r\n"); for(;;){}
}
