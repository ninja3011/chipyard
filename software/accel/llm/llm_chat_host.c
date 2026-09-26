// PC harness for llm_chat.h: same chat logic on stdin/stdout (plain-C matvec).
//   printf 'Once upon a time\n' | ./llm_chat_host stories15M_i8.bin
#include <stdio.h>
#include <stdlib.h>
#define MODEL_15M
#include "llm_chat.h"
void chat_putc(char c) { if (c != '\r') putchar(c); fflush(stdout); }
int chat_getc(void) { return getchar(); }
uint64_t chat_ticks(void) { return 0; }
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
  uint8_t *blob = aligned_alloc(8, (n + 7) / 8 * 8); if (fread(blob, 1, n, f) != (size_t)n) return 1;
  chat_main(blob); return 0;
}
