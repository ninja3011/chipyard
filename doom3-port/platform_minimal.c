/*
 * Minimal platform stub for RISC-V
 */
#include <stdio.h>
#include <stdint.h>

void platform_init() {
    printf("[PLATFORM] RV64 platform initialized\n");
}

void platform_draw_frame() {
    /* Stub */
}

uint32_t platform_get_ticks() {
    static uint32_t ticks = 0;
    return ticks++;
}

void platform_sleep_ms(uint32_t ms) {
    /* Busy wait - stub */
    volatile uint32_t count = ms * 1000;
    while (count--);
}
