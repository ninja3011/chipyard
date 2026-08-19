# Ultra-simple QEMU RISC-V test
# Just verify execution happens

.section .text
.global _start

_start:
    # Load success code
    li a0, 42

    # Infinite loop (QEMU will timeout after we can verify it ran)
    j loop

loop:
    j loop

.end
