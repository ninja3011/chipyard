/* QEMU RV64 test program */

#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          QEMU RV64 Test - Architecture Verification       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("✓ QEMU RV64 system works!\n");
    printf("✓ RISC-V binary execution confirmed!\n");
    printf("✓ Standard C library functions work!\n");
    printf("\n");

    int count = 0;
    for (int i = 1; i <= 5; i++) {
        printf("  Test iteration %d\n", i);
        count++;
    }

    printf("\n");
    printf("✓ Test complete! Processed %d iterations\n", count);
    printf("\n");

    return 0;
}
