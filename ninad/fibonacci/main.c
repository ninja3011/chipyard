// Fibonacci sequence, bare-metal TinyRocket.
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

static void print_uint(unsigned long v) {
    char buf[16];
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) uart_putc(buf[--i]);
}

#define NTERMS 40 // fib(40) = 102334155, well within 32-bit unsigned

int main(void) {
    uart_init();
    print_str("Fibonacci (TinyRocket)\r\n");

    unsigned long a = 0, b = 1;
    int i;
    for (i = 0; i < NTERMS; i++) {
        print_str("fib(");
        print_uint((unsigned long)i);
        print_str(") = ");
        print_uint(a);
        print_str("\r\n");
        unsigned long next = a + b;
        a = b;
        b = next;
    }

    print_str("Done.\r\n");
    return 0;
}
