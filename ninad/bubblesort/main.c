// Bubble sort demo, bare-metal TinyRocket. Generates its own pseudo-random
// array (a tiny hand-rolled LCG -- no libc rand()), prints it, sorts it in
// place, prints it again.
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

#define N 20

static void print_array(int *a, int n) {
    int i;
    for (i = 0; i < n; i++) {
        print_uint((unsigned long)a[i]);
        uart_putc(' ');
    }
    print_str("\r\n");
}

int main(void) {
    uart_init();
    print_str("Bubble sort (TinyRocket)\r\n");

    int arr[N];
    unsigned int lcg = 12345;
    int i, j;
    for (i = 0; i < N; i++) {
        lcg = lcg * 1103515245u + 12345u;
        arr[i] = (int)((lcg >> 16) % 100);
    }

    print_str("Before: ");
    print_array(arr, N);

    for (i = 0; i < N - 1; i++) {
        for (j = 0; j < N - 1 - i; j++) {
            if (arr[j] > arr[j + 1]) {
                int tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }

    print_str("After:  ");
    print_array(arr, N);

    print_str("Done.\r\n");
    return 0;
}
