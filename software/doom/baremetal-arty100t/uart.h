#ifndef ARTY100T_UART_H
#define ARTY100T_UART_H

#include <stdint.h>

/*
 * SiFive UART0 on RocketArty100TConfig, confirmed from the real generated
 * address map (fpga/generated-src/.../*.chisel.log and *.dts) after
 * elaborating this exact config on 2026-08-28:
 *
 *   serial@10020000 { compatible = "sifive,uart0"; reg = <0x10020000 0x1000>; }
 *   pbus_clock { clock-frequency = <50000000>; }   // 50MHz, matches
 *                                                   // WithArty100TTweaks default
 *
 * This is the same UART the `uart_tsi` docs describe using for basic
 * console I/O; here it's also used for real interactive keyboard input,
 * polled directly instead of routed through the TSI syscall channel.
 */
#define UART0_BASE 0x10020000UL

#define UART_REG_TXFIFO 0x00
#define UART_REG_RXFIFO 0x04
#define UART_REG_TXCTRL 0x08
#define UART_REG_RXCTRL 0x0C
#define UART_REG_IE     0x10
#define UART_REG_IP     0x14
#define UART_REG_DIV    0x18

#define UART_TXFIFO_FULL  (1U << 31)
#define UART_RXFIFO_EMPTY (1U << 31)
#define UART_TXCTRL_TXEN  (1U << 0)
#define UART_RXCTRL_RXEN  (1U << 0)

static inline void uart_reg_write(uint32_t off, uint32_t val) {
  *(volatile uint32_t*)(UART0_BASE + off) = val;
}

static inline uint32_t uart_reg_read(uint32_t off) {
  return *(volatile uint32_t*)(UART0_BASE + off);
}

/* div = (f_clk / baud) - 1. 50MHz / 115200 - 1 = 433 (rounded). */
#define UART_DIV_115200 433

static inline void uart_init(void) {
  uart_reg_write(UART_REG_DIV, UART_DIV_115200);
  uart_reg_write(UART_REG_TXCTRL, UART_TXCTRL_TXEN);
  uart_reg_write(UART_REG_RXCTRL, UART_RXCTRL_RXEN);
}

static inline void uart_putc(char c) {
  while (uart_reg_read(UART_REG_TXFIFO) & UART_TXFIFO_FULL)
    ;
  uart_reg_write(UART_REG_TXFIFO, (uint32_t)(uint8_t)c);
}

static inline void uart_puts(const char *s) {
  while (*s) uart_putc(*s++);
}

/* Non-blocking: returns -1 if no byte is waiting, else the byte (0-255). */
static inline int uart_getc_nonblock(void) {
  uint32_t v = uart_reg_read(UART_REG_RXFIFO);
  if (v & UART_RXFIFO_EMPTY) return -1;
  return (int)(v & 0xFF);
}

#endif /* ARTY100T_UART_H */
