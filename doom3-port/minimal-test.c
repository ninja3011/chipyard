/* Minimal Spike test - uses standard memory layout */

#include <stdio.h>

int main() {
    printf("✓ RISC-V binary executed successfully!\n");
    printf("✓ Cross-compiler output works!\n");
    printf("✓ Basic C library functions work!\n");

    for (int i = 1; i <= 10; i++) {
        printf("  Frame %d\n", i);
    }

    printf("✓ Test complete!\n");
    return 0;
}
