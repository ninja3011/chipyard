// Multiplication table, bare-metal TinyRocket.
#define UART_BASE       0x10020000UL
#define UART_TXDATA     (*(volatile unsigned int *)(UART_BASE + 0x00))
#define UART_TXCTRL     (*(volatile unsigned int *)(UART_BASE + 0x08))
#define UART_DIV        (*(volatile unsigned int *)(UART_BASE + 0x18))
#define SYS_CLK_HZ      10000000UL
#define UART_BAUD       115200UL

static void uart_init(void) {
    UART_DIV = (SYS_CLK_HZ / UART_BAUD) - 1;
    UART_TXCTRL = 0x1;
}
static void uart_putc(char c) {
    while (UART_TXDATA & (1u << 31)) { }
    UART_TXDATA = (unsigned char)c;
}
static void print_str(const char *s) { while (*s) uart_putc(*s++); }

static void print_uint_padded(unsigned long v, int width) {
    char buf[16];
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i < width) buf[i++] = ' '; // pad, then print reversed -> right-pad becomes left space when flushed below
    while (i > 0) uart_putc(buf[--i]);
}

#define N 12

int main(void) {
    uart_init();
    print_str("Multiplication table 1-12 (TinyRocket)\r\n\r\n");

    int i, j;
    print_str("    ");
    for (j = 1; j <= N; j++) { print_uint_padded((unsigned long)j, 4); }
    print_str("\r\n");

    for (i = 1; i <= N; i++) {
        print_uint_padded((unsigned long)i, 4);
        for (j = 1; j <= N; j++) {
            print_uint_padded((unsigned long)(i * j), 4);
        }
        print_str("\r\n");
    }

    print_str("\r\nDone.\r\n");
    return 0;
}
