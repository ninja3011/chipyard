/* Minimal, fully freestanding _start for mini_raycaster.c -- no newlib,
 * no libc_init_array, no malloc, nothing. Everything this program needs
 * (BSS clear, trap handler, stack) is self-contained right here, so
 * there is no zone allocator, no WAD parser, no decades-old assumptions
 * to debug -- if this crashes, the bug is in ~500 lines of code we wrote
 * tonight, not in a 30-year-old codebase.
 *
 * Reuses the exact fixes already proven on this hardware tonight:
 *   - sp set explicitly before anything else touches it (crt0 never did)
 *   - mstatus.MIE/mie cleared before any C code runs (async interrupts)
 *   - a real mtvec pointing at a handler that records mcause/mepc/mtval
 *     (synchronous exceptions aren't masked by the above)
 */

.section .text._start
.global _start
_start:
    .option push
    .option norelax
    li      sp, 0x87000000

    csrci   mstatus, 8
    csrw    mie, zero

    /* Enable the FPU (mstatus.FS, bits 14:13) before any C code runs.
     * RISC-V requires this explicitly -- with FS left at its reset value
     * of 0 (Off), ANY floating-point instruction traps as illegal,
     * including the callee-saved fs-register spills GCC's function
     * prologues insert automatically whenever a function uses doubles
     * (mini_raycaster.c's Player struct does) -- even before the
     * function's own first line of code runs. mini_hello.c never
     * touches FP at all, which is exactly why it worked without this.
     */
    li      t0, 0x00006000
    csrs    mstatus, t0

    /* Zero the trap-record slots at boot so a value read back later is
     * guaranteed to come from THIS run. DRAM survives a reset, and
     * uart_tsi's ELF load never touches this region, so a stale
     * mcause/mepc from an older crash otherwise sits here indefinitely
     * and masquerades as a fresh result -- that exact trap cost real
     * time twice this session. */
    li      t0, 0x86000200
    sw      zero, 0(t0)
    sw      zero, 4(t0)
    sw      zero, 8(t0)
    sw      zero, 12(t0)

    la      t0, trap_entry
    csrw    mtvec, t0

    1:
    auipc   gp, %pcrel_hi(__global_pointer$)
    addi    gp, gp, %pcrel_lo(1b)
    .option pop

    /* clear BSS with a plain byte loop -- no libc memset needed */
    la      t0, __bss_start
    la      t1, _end
clear_bss_loop:
    bgeu    t0, t1, clear_bss_done
    sb      zero, 0(t0)
    addi    t0, t0, 1
    j       clear_bss_loop
clear_bss_done:

    call    main

spin_forever:
    j       spin_forever

.align 4
trap_entry:
    li      t0, 0x86000200
    li      t1, 0xc0defa17
    sw      t1, 0(t0)
    csrr    t1, mcause
    sw      t1, 4(t0)
    csrr    t1, mepc
    sw      t1, 8(t0)
    csrr    t1, mtval
    sw      t1, 12(t0)
trap_halt:
    j       trap_halt
