# QEMU RV64 bare-metal bootloader
# Minimal startup code for QEMU's -kernel mode

.section .text.init
.global _start

_start:
    # Initialize stack
    la sp, _stack_top

    # Jump to main
    jal ra, main

    # Loop forever if main returns
1:  j 1b

.section .bss
.align 16
_stack:
    .space 4096
_stack_top:
