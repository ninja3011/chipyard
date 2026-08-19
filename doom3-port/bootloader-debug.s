# Debug bootloader - prints exit code via HTIF
.section .text
.global _start
.align 4

_start:
    # Initialize stack
    li sp, 0xA0000000

    # Set up exception handler
    la t0, exception_handler
    csrw stvec, t0

    # Initialize CSRs
    csrw sie, zero
    csrw sip, zero

    # Jump to game logic
    call main

    # Write exit code to HTIF tohost
    # Format: (exit_code << 1) | 1  (bit 0 set = success)
    la t0, tohost

    # a0 = return value from main()
    # Left shift by 1 and OR with 1 to signal exit
    slli t1, a0, 1
    ori t1, t1, 1
    sd t1, 0(t0)

    # Wait for host to detect exit and terminate
    j .    # Loop forever at this address (simulator reads tohost and terminates)

.align 4
exception_handler:
    j exception_handler
