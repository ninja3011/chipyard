// SiFive UART0 (chipyard.config.WithUART), instantiated at 0x10020000 in
// TinyRocketConfig -- confirmed from the generated
// chipyard.harness.TestHarness.TinyRocketConfig.dts ("serial@10020000") and
// the regmap json for that address. The previous 0x64000000 was wrong for
// this config (that address is only correct for configs that place the UART
// on a different bus layout).
#define UART_BASE       0x10020000UL
#define UART_TXDATA     (*(volatile unsigned int *)(UART_BASE + 0x00)) // [7:0] data, [31] full
#define UART_TXCTRL     (*(volatile unsigned int *)(UART_BASE + 0x08)) // [0] txen, [1] nstop
#define UART_DIV        (*(volatile unsigned int *)(UART_BASE + 0x18)) // [15:0] baud divisor

// UART_DIV resets to 4340, which is only correct if the peripheral bus clock
// is actually 500MHz (the DTS default used at Chisel-elaboration time). On
// Basys3 the design is driven by a Clocking Wizard output well below that
// (see basys3/BasysTop.v and basys3.xdc -- clk_out1 is configured for
// 10MHz), so the divisor is reprogrammed at boot instead of relying on the
// reset value. Keep this in sync with whatever clk_out1 is actually set to.
#define SYS_CLK_HZ      10000000UL
#define UART_BAUD       115200UL

static void uart_init(void) {
    UART_DIV = (SYS_CLK_HZ / UART_BAUD) - 1;
    UART_TXCTRL = 0x1; // txen = 1 (resets to 0 -- TX is disabled until this is set)
}

static void uart_putc(char c) {
    while (UART_TXDATA & (1u << 31)) { } // spin while TX FIFO full
    UART_TXDATA = (unsigned char)c;
}

static void print_str(const char *str) {
    while (*str) {
        uart_putc(*str++);
    }
}

int main() {
    uart_init();
    print_str("Hello World from TinyRocket!\n");
    return 0;
}
