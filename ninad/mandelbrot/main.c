// Mandelbrot set, rendered as ASCII art over UART, on bare-metal TinyRocket.
//
// No hardware floating point on this core (ISA is rv32imac -- no F/D
// extension), so this uses fixed-point Q8.8 arithmetic (8 integer bits, 8
// fractional bits, stored in a 32-bit int) instead of float/double. All
// values involved stay well under 2^15 in magnitude (escape radius is 2.0,
// i.e. 512 in Q8.8), so plain 32-bit multiply never overflows -- no need
// for 64-bit intermediate math or libgcc helpers.

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

static void print_str(const char *str) {
    while (*str) uart_putc(*str++);
}

typedef int fp_t; // Q8.8 fixed point

#define FP_SHIFT 8
#define FP_ONE   (1 << FP_SHIFT)

static fp_t fp_mul(fp_t a, fp_t b) {
    return (fp_t)(((int)a * (int)b) >> FP_SHIFT);
}

#define WIDTH   78
#define HEIGHT  24
#define MAXITER 24

static int mandel_iters(fp_t cr, fp_t ci) {
    fp_t zr = 0, zi = 0;
    int i;
    for (i = 0; i < MAXITER; i++) {
        fp_t zr2 = fp_mul(zr, zr);
        fp_t zi2 = fp_mul(zi, zi);
        if (zr2 + zi2 > (4 << FP_SHIFT)) break;
        fp_t new_zi = fp_mul(zr, zi) * 2 + ci;
        fp_t new_zr = zr2 - zi2 + cr;
        zr = new_zr;
        zi = new_zi;
    }
    return i;
}

static const char ramp[] = " .:-=+*#%@";
#define RAMP_LEN ((int)(sizeof(ramp) - 1))

int main(void) {
    uart_init();
    print_str("Mandelbrot (TinyRocket, Q8.8 fixed point)\r\n");

    fp_t x0 = -2 * FP_ONE, x1 = 1 * FP_ONE; // real axis  [-2.0, 1.0]
    fp_t y0 = -1 * FP_ONE, y1 = 1 * FP_ONE; // imag axis  [-1.0, 1.0]
    fp_t dx = (x1 - x0) / WIDTH;
    fp_t dy = (y1 - y0) / HEIGHT;

    int py, px;
    for (py = 0; py < HEIGHT; py++) {
        fp_t ci = y0 + dy * py;
        for (px = 0; px < WIDTH; px++) {
            fp_t cr = x0 + dx * px;
            int it = mandel_iters(cr, ci);
            int idx = (it * RAMP_LEN) / MAXITER;
            if (idx >= RAMP_LEN) idx = RAMP_LEN - 1;
            uart_putc(ramp[idx]);
        }
        uart_putc('\r');
        uart_putc('\n');
    }

    print_str("Done.\r\n");
    return 0;
}
