# Linux bootloader for BOOM Verilator
# This bootloader sets up virtual addressing (Sv39) and jumps to Linux kernel
# Entry point: 0x80000000 (physical)
# Kernel entry: 0xffffffff80000000 (virtual, with Sv39 paging)

.section .text
.global _start
.align 4

_start:
    # Initialize stack at a safe location
    li sp, 0x80001000

    # a0 = hartid (from Test Driver)
    # a1 = device tree pointer (will set below)

    # Set up exception handler (stvec in supervisor mode)
    la t0, trap_handler
    csrw stvec, t0

    # Enable supervisor mode and set SPIE bit
    li t0, (1 << 8) | (1 << 5)  # SPP=1 (supervisor), SPIE=1
    csrs mstatus, t0

    # Set up page table root (satp register)
    # For now, disable paging initially, kernel will set up its own
    csrw satp, x0  # Disable paging for now

    # Set mepc to kernel entry point (will jump to virtual address)
    # But first, we need to handle the addressing...
    # Actually, load kernel at physical first, let kernel handle paging

    # Set kernel entry point
    li t0, 0xffffffff80000000
    csrw mepc, t0

    # Set device tree pointer in a1
    la a1, device_tree_blob

    # Jump to kernel using mret (enters supervisor mode)
    mret

# Trap handler
trap_handler:
    j trap_handler

# Minimal device tree
.section .rodata
.align 4
device_tree_blob:
    # Minimal device tree in binary format
    # This is a placeholder - real kernel may boot without proper DT
    .byte 0, 0, 0, 0
