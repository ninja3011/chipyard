/* Minimal test that exits to address 0 to terminate Spike */

int main() {
    /* Do some simple computation to verify execution */
    volatile int result = 0;
    result += 40;
    result += 2;

    if (result == 42) {
        /* Jump to address 0 to exit Spike  */
        asm volatile("j 0");
    }

    return 0;
}
