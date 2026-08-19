# Bare-metal bootloader for DOOM on BOOM
# Entry point: 0x80000000 (physical memory start)
# Goal: Initialize CPU and jump to DOOM game logic

.section .text
.global _start
.align 4

_start:
    # Initialize stack pointer at high memory (512MB boundary)
    li sp, 0xA0000000

    # Initialize global pointer for small data access
    la gp, _global_pointer

    # Set up exception/interrupt handler (stvec in supervisor mode)
    la t0, exception_handler
    csrw stvec, t0

    # Initialize a few CSRs for safety
    csrw sie, zero         # Disable interrupts initially
    csrw sip, zero         # Clear pending interrupts

    # Jump to main C code (DOOM game loop)
    call main

    # When main returns, write exit code to tohost (HTIF protocol)
    # a0 contains return value/exit code
    la t0, tohost
    sd a0, 0(t0)

    # Loop forever (simulator will detect exit code via tohost)
    j _start

# Exception handler (minimal - just loop)
.align 4
exception_handler:
    j exception_handler

# Alignment marker for data sections
.global _global_pointer
_global_pointer:
    .word 0

# Minimal trap vector
.align 4
.global _trap_vector
_trap_vector:
    j exception_handler
