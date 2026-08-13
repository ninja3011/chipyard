.section .text.init
.global _start

_start:
    /* Set up the stack pointer to the very end of the 16KB DTIM */
    li sp, 0x80004000

    /* Zero .bss before running any C code. The DTIM is real SRAM and a CPU
       reset does not clear it, so without this, a global/static array with
       no explicit initializer (like Game of Life's grid[][] or the prime
       sieve's is_composite[]) can start with whatever bytes a *previous*
       program left behind at that address, instead of the all-zero state
       the C standard promises for .bss. Every real C runtime does this
       zeroing step in its own startup code (crt0) before main() -- this is
       that step, written by hand since we have no libc/crt0 here. A
       program with no .bss (_bss_start == _bss_end) just skips the loop
       immediately, so this is a harmless no-op for those. */
    la a0, _bss_start
    la a1, _bss_end
bss_zero_loop:
    bgeu a0, a1, bss_zero_done
    sb zero, 0(a0)
    addi a0, a0, 1
    j bss_zero_loop
bss_zero_done:

    /* Jump into your C code! */
    call main

loop:
    /* If main() ever exits, trap the CPU in an infinite loop */
    j loop
