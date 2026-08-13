// RocketCalc: Compact Scientific Calculator on bare-metal TinyRocket
// sin, pow, sqrt + basic arithmetic
#define UART_BASE       0x10020000UL
#define UART_TXDATA     (*(volatile unsigned int *)(UART_BASE + 0x00))
#define UART_RXDATA     (*(volatile unsigned int *)(UART_BASE + 0x04))
#define UART_TXCTRL     (*(volatile unsigned int *)(UART_BASE + 0x08))
#define UART_RXCTRL     (*(volatile unsigned int *)(UART_BASE + 0x0c))
#define UART_DIV        (*(volatile unsigned int *)(UART_BASE + 0x18))

#define SYS_CLK_HZ      10000000UL
#define UART_BAUD       115200UL

typedef long long i64;

i64 __divdi3(i64 a, i64 b) {
    if (b == 0) return 0;
    int neg_a = (a < 0), neg_b = (b < 0);
    if (neg_a) a = -a;
    if (neg_b) b = -b;
    i64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { q |= (1LL << i); r -= b; }
    }
    return (neg_a ^ neg_b) ? -q : q;
}

i64 __moddi3(i64 a, i64 b) {
    if (b == 0) return 0;
    int neg = (a < 0);
    if (neg) a = -a;
    if (b < 0) b = -b;
    i64 r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) r -= b;
    }
    return neg ? -r : r;
}

#define FP_ONE (1LL << 16)
#define FP_PI  (3141592653589793LL / 1000000000LL)
#define FP_E   (2718281828LL / 1000000LL)

static void uart_init(void) {
    UART_DIV = (SYS_CLK_HZ / UART_BAUD) - 1;
    UART_TXCTRL = 0x1;
    UART_RXCTRL = 0x1;
}

static void uart_putc(char c) {
    while (UART_TXDATA & (1u << 31)) { }
    UART_TXDATA = (unsigned char)c;
}

static void print_str(const char *s) {
    while (*s) uart_putc(*s++);
}

static int uart_getc(void) {
    unsigned int v;
    do { v = UART_RXDATA; } while (v & (1u << 31));
    return (int)(v & 0xFF);
}

static void print_int(long v) {
    char buf[20];
    int i = 0;
    int neg = (v < 0);
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (u == 0) buf[i++] = '0';
    while (u) { buf[i++] = (char)('0' + (u % 10)); u /= 10; }
    if (neg) buf[i++] = '-';
    while (i > 0) uart_putc(buf[--i]);
}

static void print_fixed(i64 q16, int digits) {
    long int_part = (long)(q16 >> 16);
    long frac_part = (long)(q16 & 0xFFFF);
    frac_part = (frac_part * 100000) >> 16;
    if (frac_part < 0) frac_part = -frac_part;
    print_int(int_part);
    uart_putc('.');
    if (frac_part < 10000) uart_putc('0');
    if (frac_part < 1000) uart_putc('0');
    if (frac_part < 100) uart_putc('0');
    if (frac_part < 10) uart_putc('0');
    print_int(frac_part);
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static i64 parse_fixed(const char *s, int *idx) {
    i64 result = 0;
    int neg = 0;
    while (s[*idx] == ' ') (*idx)++;
    if (s[*idx] == '-') { neg = 1; (*idx)++; }
    while (is_digit(s[*idx])) {
        result = result * 10 + (s[*idx] - '0');
        (*idx)++;
    }
    result <<= 16;
    if (s[*idx] == '.') {
        (*idx)++;
        i64 frac = 0;
        int decimals = 0;
        while (is_digit(s[*idx]) && decimals < 5) {
            frac = frac * 10 + (s[*idx] - '0');
            (*idx)++;
            decimals++;
        }
        while (decimals < 5) { frac *= 10; decimals++; }
        result += (frac * 65536UL) / 100000UL;
    }
    return neg ? -result : result;
}

static void skip_spaces(const char *s, int *idx) {
    while (s[*idx] == ' ') (*idx)++;
}

static int match_word(const char *s, int *idx, const char *word) {
    int i = 0;
    skip_spaces(s, idx);
    while (word[i] && (s[*idx + i] == word[i] ||
           (s[*idx + i] >= 'A' && s[*idx + i] <= 'Z' &&
            s[*idx + i] - 'A' + 'a' == word[i]))) i++;
    if (word[i] == 0 && !is_alpha(s[*idx + i])) {
        *idx += i;
        return 1;
    }
    return 0;
}

static const short sin_table[91] = {
    0, 4, 9, 13, 18, 22, 27, 31, 36, 40,
    44, 49, 53, 58, 62, 66, 71, 75, 79, 83,
    88, 92, 96, 100, 104, 108, 112, 116, 120, 124,
    128, 132, 136, 139, 143, 147, 150, 154, 158, 161,
    165, 168, 171, 175, 178, 181, 184, 187, 190, 193,
    196, 199, 202, 204, 207, 210, 212, 215, 217, 219,
    222, 224, 226, 228, 230, 232, 234, 236, 237, 239,
    241, 242, 243, 245, 246, 247, 248, 249, 250, 251,
    252, 253, 254, 254, 255, 255, 255, 256, 256, 256, 256
};

static i64 sin_deg(int d) {
    d = ((d % 360) + 360) % 360;
    int sign = 1;
    if (d > 180) { d = 360 - d; sign = -1; }
    if (d > 90) d = 180 - d;
    i64 result = sin_table[d];
    if (d != 0) result = (result * FP_ONE) / 256;
    return sign > 0 ? result : -result;
}

static i64 isqrt(i64 x) {
    if (x < 0) return 0;
    if (x < (2LL << 16)) return FP_ONE;
    i64 root = x >> 1;
    for (int i = 0; i < 20; i++) {
        root = (root + (x / root)) >> 1;
    }
    return root;
}

static i64 ipow(i64 base, int exp) {
    if (exp < 0) return 0;
    i64 result = FP_ONE;
    for (int i = 0; i < exp; i++) {
        result = (result * base) >> 16;
        if (result > 1000000LL << 16) break;
    }
    return result;
}

static void print_help(void) {
    print_str("\r\n");
    print_str(" ╔═══════════════════════════════════╗\r\n");
    print_str(" ║   RocketCalc - Science Edition    ║\r\n");
    print_str(" ║  Compact Calculator on TinyRocket  ║\r\n");
    print_str(" ╚═══════════════════════════════════╝\r\n");
    print_str("\r\n SCIENCE COMMANDS:\r\n");
    print_str("   sin <degrees>      - Sine\r\n");
    print_str("   pow <base> <exp>   - Power\r\n");
    print_str("   sqrt <x>           - Square root\r\n");
    print_str("\r\n BASIC OPS:\r\n");
    print_str("   <a> + <b>  <a> - <b>  <a> * <b>  <a> / <b>\r\n");
    print_str("\r\n CONSTANTS:\r\n");
    print_str("   pi    e\r\n");
    print_str("\r\n EXAMPLES:\r\n");
    print_str("   sin 45        pow 2 10      sqrt 16\r\n");
    print_str("   10 + 5        100 / 4       pi\r\n");
    print_str("\r\n");
}

int main(void) {
    uart_init();
    print_str("\r\n");
    print_str(" ╔═════════════════════════════════╗\r\n");
    print_str(" ║      >>> RocketCalc v1.0 <<<    ║\r\n");
    print_str(" ║   Scientific Calc on TinyRocket  ║\r\n");
    print_str(" ╚═════════════════════════════════╝\r\n");
    print_str("\r\nType 'help' for commands\r\n\r\n");

    char buf[64];
    for (;;) {
        print_str("calc> ");
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
        skip_spaces(buf, &idx);

        if (match_word(buf, &idx, "help")) {
            print_help();
            continue;
        }
        if (match_word(buf, &idx, "clear")) {
            print_str("\033[2J\033[H");
            continue;
        }
        if (match_word(buf, &idx, "quit") || match_word(buf, &idx, "exit")) {
            print_str("Goodbye!\r\n");
            break;
        }
        if (match_word(buf, &idx, "pi")) {
            print_str("= ");
            print_fixed(FP_PI, 5);
            print_str("\r\n");
            continue;
        }
        if (match_word(buf, &idx, "e")) {
            print_str("= ");
            print_fixed(FP_E, 5);
            print_str("\r\n");
            continue;
        }

        if (match_word(buf, &idx, "sin")) {
            int angle = (int)parse_fixed(buf, &idx);
            i64 result = sin_deg(angle >> 16);
            print_str("= ");
            print_fixed(result, 5);
            print_str("\r\n");
            continue;
        }
        if (match_word(buf, &idx, "sqrt")) {
            i64 x = parse_fixed(buf, &idx);
            if (x < 0) { print_str("error: negative sqrt\r\n"); continue; }
            i64 result = isqrt(x);
            print_str("= ");
            print_fixed(result, 5);
            print_str("\r\n");
            continue;
        }
        if (match_word(buf, &idx, "pow")) {
            i64 base = parse_fixed(buf, &idx);
            int exp = (int)parse_fixed(buf, &idx);
            if (exp < 0) { print_str("error: negative exponent\r\n"); continue; }
            i64 result = ipow(base, exp >> 16);
            print_str("= ");
            print_fixed(result, 5);
            print_str("\r\n");
            continue;
        }

        i64 a = parse_fixed(buf, &idx);
        skip_spaces(buf, &idx);
        char op = buf[idx];
        if (op) idx++;
        i64 b = parse_fixed(buf, &idx);

        i64 result = 0;
        int valid = 1;

        if (op == '+') result = a + b;
        else if (op == '-') result = a - b;
        else if (op == '*') result = (a * b) >> 16;
        else if (op == '/') {
            if (b == 0) { print_str("error: division by zero\r\n"); valid = 0; }
            else result = (a << 16) / b;
        }
        else {
            print_str("? type 'help' for commands\r\n");
            continue;
        }

        if (valid) {
            print_str("= ");
            print_fixed(result, 5);
            print_str("\r\n");
        }
    }
    return 0;
}
