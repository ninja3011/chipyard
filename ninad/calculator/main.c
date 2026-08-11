// Interactive calculator over UART, bare-metal TinyRocket.
//
// This is the first program in the set that actually uses uart_0_rxd --
// every earlier program only ever wrote to UART. Register offsets (rxdata
// at +0x04, its "empty" flag at bit 31, rxen at +0x0c bit 0) are taken from
// the same regmap json used to validate the TXDATA/TXCTRL offsets in the
// original Hello World firmware, not guessed.
//
// Protocol: type "<int> <op> <int>" and press Enter, e.g. "12 + 7". Supports
// + - * /. No libc (no scanf/atoi) -- everything here is hand-rolled.

#define UART_BASE       0x10020000UL
#define UART_TXDATA     (*(volatile unsigned int *)(UART_BASE + 0x00))
#define UART_RXDATA     (*(volatile unsigned int *)(UART_BASE + 0x04))
#define UART_TXCTRL     (*(volatile unsigned int *)(UART_BASE + 0x08))
#define UART_RXCTRL     (*(volatile unsigned int *)(UART_BASE + 0x0c))
#define UART_DIV        (*(volatile unsigned int *)(UART_BASE + 0x18))

#define SYS_CLK_HZ      10000000UL
#define UART_BAUD       115200UL

static void uart_init(void) {
    UART_DIV = (SYS_CLK_HZ / UART_BAUD) - 1;
    UART_TXCTRL = 0x1; // txen
    UART_RXCTRL = 0x1; // rxen
}

static void uart_putc(char c) {
    while (UART_TXDATA & (1u << 31)) { } // spin while TX FIFO full
    UART_TXDATA = (unsigned char)c;
}

static void print_str(const char *s) {
    while (*s) uart_putc(*s++);
}

static int uart_getc(void) {
    unsigned int v;
    do {
        v = UART_RXDATA;
    } while (v & (1u << 31)); // bit31 = rxdata "empty"
    return (int)(v & 0xFF);
}

static void print_int(long v) {
    char buf[16];
    int i = 0;
    int neg = (v < 0);
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (u == 0) buf[i++] = '0';
    while (u) { buf[i++] = (char)('0' + (u % 10)); u /= 10; }
    if (neg) buf[i++] = '-';
    while (i > 0) uart_putc(buf[--i]);
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

int main(void) {
    uart_init();
    print_str("Calculator (TinyRocket)\r\n");
    print_str("Enter: <int> <op> <int>  e.g. 12 + 7   (ops: + - * /)\r\n");

    char buf[32];
    for (;;) {
        print_str("> ");
        int n = 0;
        for (;;) {
            int c = uart_getc();
            if (c == '\r' || c == '\n') {
                uart_putc('\r'); uart_putc('\n');
                break;
            }
            if ((c == '\b' || c == 127) && n > 0) {
                n--;
                print_str("\b \b");
                continue;
            }
            if (n < (int)sizeof(buf) - 1 && c >= 32 && c < 127) {
                buf[n++] = (char)c;
                uart_putc((char)c);
            }
        }
        buf[n] = 0;

        int idx = 0;
        while (buf[idx] == ' ') idx++;

        int neg1 = 0;
        if (buf[idx] == '-') { neg1 = 1; idx++; }
        long a = 0;
        int have_a = 0;
        while (is_digit(buf[idx])) { a = a * 10 + (buf[idx] - '0'); idx++; have_a = 1; }
        if (neg1) a = -a;

        while (buf[idx] == ' ') idx++;
        char op = buf[idx];
        if (op) idx++;
        while (buf[idx] == ' ') idx++;

        int neg2 = 0;
        if (buf[idx] == '-') { neg2 = 1; idx++; }
        long b = 0;
        int have_b = 0;
        while (is_digit(buf[idx])) { b = b * 10 + (buf[idx] - '0'); idx++; have_b = 1; }
        if (neg2) b = -b;

        if (!have_a || !have_b || (op != '+' && op != '-' && op != '*' && op != '/')) {
            print_str("? try: 12 + 7\r\n");
            continue;
        }

        long r;
        if (op == '+') r = a + b;
        else if (op == '-') r = a - b;
        else if (op == '*') r = a * b;
        else {
            if (b == 0) { print_str("error: division by zero\r\n"); continue; }
            r = a / b;
        }

        print_str("= ");
        print_int(r);
        print_str("\r\n");
    }
}
