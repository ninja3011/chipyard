.section .text.init
.global _start

_start:
    /* Set up the stack pointer to the very end of the 16KB DTIM */
    li sp, 0x80004000
    
    /* Jump into your C code! */
    call main

loop:
    /* If main() ever exits, trap the CPU in an infinite loop */
    j loop
