.section .text.start
.globl _start
_start:
    # Initialize stack pointer
    la sp, _stack_end

    # Initialize UART (write base address to UART control register)
    # For now, just jump to main (OS will handle UART later)

    # Load kernel address from memory
    la a0, 0x80000000  # Kernel base address for Linux

    # Jump to kernel entry point
    jr a0

# Minimal exception handler
.align 4
.globl exception_handler
exception_handler:
    # For now, just hang
    j exception_handler

.section .bss
.align 12
_stack:
    .skip 4096
_stack_end:
