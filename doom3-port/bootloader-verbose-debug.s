# Ultra-verbose bootloader for DOOM - maximum debug output
# Shows every step of boot process

.section .text
.global _start
.align 4

_start:
    # Print: "BOOT_START"
    li t0, 0x10010000  # UART address (BOOM standard)
    li t1, 'B'
    sw t1, 0(t0)
    li t1, 'O'
    sw t1, 0(t0)
    li t1, 'O'
    sw t1, 0(t0)
    li t1, 'T'
    sw t1, 0(t0)

    # Initialize stack
    li sp, 0xA0000000

    # Print: "STACK"
    li t1, '_'
    sw t1, 0(t0)
    li t1, 'S'
    sw t1, 0(t0)
    li t1, 'T'
    sw t1, 0(t0)

    # Set up exception handler
    la t2, exception_handler
    csrw stvec, t2

    # Print: "EXC"
    li t1, 'E'
    sw t1, 0(t0)
    li t1, 'X'
    sw t1, 0(t0)
    li t1, 'C'
    sw t1, 0(t0)

    # Initialize CSRs
    csrw sie, zero
    csrw sip, zero

    # Print: "CSR"
    li t1, 'C'
    sw t1, 0(t0)
    li t1, 'S'
    sw t1, 0(t0)
    li t1, 'R'
    sw t1, 0(t0)

    # Print newline
    li t1, '\n'
    sw t1, 0(t0)

    # Jump to game logic
    li t1, 'J'
    sw t1, 0(t0)
    li t1, 'M'
    sw t1, 0(t0)
    li t1, 'P'
    sw t1, 0(t0)
    li t1, '\n'
    sw t1, 0(t0)

    call main

    # If main returns, exit
    # a0 = return value
    li t0, 0x82000010  # tohost address
    slli t1, a0, 1
    ori t1, t1, 1
    sd t1, 0(t0)

    # Loop forever
    j .

.align 4
exception_handler:
    # Print "EXC_HIT"
    li t0, 0x10010000
    li t1, 'E'
    sw t1, 0(t0)
    li t1, 'H'
    sw t1, 0(t0)
    li t1, 'I'
    sw t1, 0(t0)
    li t1, 'T'
    sw t1, 0(t0)
    li t1, '\n'
    sw t1, 0(t0)

    j exception_handler
