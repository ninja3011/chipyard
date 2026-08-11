// Conway's Game of Life, bare-metal TinyRocket. Toroidal (wrap-around)
// grid, seeded with a glider plus a couple of oscillators, printed as
// ASCII each generation over UART.
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

#define W 40
#define H 16
#define GENERATIONS 15

static unsigned char grid[H][W];
static unsigned char next_grid[H][W];

static int alive_count(int y, int x) {
    int count = 0, dy, dx;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            if (dy == 0 && dx == 0) continue;
            int ny = (y + dy + H) % H;
            int nx = (x + dx + W) % W;
            count += grid[ny][nx];
        }
    }
    return count;
}

static void print_grid(void) {
    int y, x;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            uart_putc(grid[y][x] ? '#' : '.');
        }
        uart_putc('\r');
        uart_putc('\n');
    }
}

int main(void) {
    uart_init();
    print_str("Conway's Game of Life (TinyRocket)\r\n\r\n");

    // Glider
    grid[1][2] = 1; grid[2][3] = 1; grid[3][1] = 1; grid[3][2] = 1; grid[3][3] = 1;
    // Blinker
    grid[10][20] = 1; grid[10][21] = 1; grid[10][22] = 1;
    // Toad
    grid[5][30] = 1; grid[5][31] = 1; grid[5][32] = 1;
    grid[6][29] = 1; grid[6][30] = 1; grid[6][31] = 1;

    int gen;
    for (gen = 0; gen < GENERATIONS; gen++) {
        print_str("-- generation ");
        print_uint((unsigned long)gen);
        print_str(" --\r\n");
        print_grid();

        int y, x;
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                int n = alive_count(y, x);
                if (grid[y][x]) {
                    next_grid[y][x] = (n == 2 || n == 3) ? 1 : 0;
                } else {
                    next_grid[y][x] = (n == 3) ? 1 : 0;
                }
            }
        }
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                grid[y][x] = next_grid[y][x];
            }
        }
    }

    print_str("Done.\r\n");
    return 0;
}
