// Sieve of Eratosthenes, bare-metal TinyRocket.
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

#define LIMIT 500

static unsigned char is_composite[LIMIT + 1];

int main(void) {
    uart_init();
    print_str("Prime sieve up to 500 (TinyRocket)\r\n");

    int i, j, count = 0;
    for (i = 2; i <= LIMIT; i++) {
        if (!is_composite[i]) {
            print_uint((unsigned long)i);
            uart_putc(' ');
            count++;
            if (count % 10 == 0) print_str("\r\n");
            for (j = i * 2; j <= LIMIT; j += i) {
                is_composite[j] = 1;
            }
        }
    }

    print_str("\r\nFound ");
    print_uint((unsigned long)count);
    print_str(" primes.\r\nDone.\r\n");
    return 0;
}
