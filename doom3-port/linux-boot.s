# Linux boot loader for BOOM Verilator
# Purpose: Initialize environment and jump to Linux kernel
# Entry point: 0x80000000 (physical)
# Kernel entry: 0xffffffff80000000 (virtual, Sv39)

.section .text
.global _start
.align 4

_start:
    # a0 = hartid (from Test Driver)
    # a1 = device tree pointer (will be set by bootloader)

    # Initialize stack at end of first page
    li sp, 0x80001000

    # Set up exception handler
    la t0, trap_handler
    csrw stvec, t0

    # Enable supervisor mode
    li t0, (1 << 8)  # SPP (supervisor previous privilege)
    csrs mstatus, t0

    # Set mepc to kernel entry point (virtual address)
    li t0, 0xffffffff80000000
    csrw mepc, t0

    # Set device tree pointer (a1)
    la a1, device_tree

    # Jump to kernel using mret (enters supervisor mode)
    mret

# Trap handler (should not be reached if kernel boots correctly)
trap_handler:
    # Infinite loop if trap occurs
    j trap_handler

# Minimal device tree embedded
.section .data
.align 4
device_tree:
    # This is a placeholder - kernel may boot without proper DT
    # Real DT would be loaded from external source
    .word 0
    .word 0

# Stack space
.section .bss
.align 16
_stack:
    .space 4096
_stack_top:
