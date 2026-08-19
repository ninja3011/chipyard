# Bare-metal RISC-V test for Spike
# No C library dependencies

.section .text
.global _start

_start:
    # Exit with success code (a0 = 0)
    li a0, 0
    j exit

exit:
    # Spike will terminate when we write to HTIF
    # For now, just loop forever (simulator will timeout)
    beq x0, x0, exit

.end
