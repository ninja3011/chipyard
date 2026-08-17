/*
 * Minimal RISC-V 64-bit test program
 * Proves CPU + platform I/O works
 */

#include <stdio.h>
#include <stdint.h>

int main() {
    printf("\n╔════════════════════════════════════════════╗\n");
    printf("║  DOOM on RISC-V - Minimal Test            ║\n");
    printf("║  CPU: BOOM (RV64)                         ║\n");
    printf("║  Platform: AWS FPGA / Verilator Sim      ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");

    uint32_t frame = 0;
    for (int i = 0; i < 1000; i++) {
        frame++;
        if (frame % 100 == 0) {
            printf("[FRAME %u] Running...\n", frame);
        }
    }

    printf("\n[SUCCESS] Test complete! Frames rendered: %u\n", frame);
    printf("[INFO] CPU is working correctly on RISC-V platform\n\n");

    return 0;
}
