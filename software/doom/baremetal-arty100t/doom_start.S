/* Replacement for the toolchain's own crt0.o.
 *
 * ROOT CAUSE FOUND (static analysis, not yet hardware-confirmed): the
 * stock crt0.o that -specs=nano.specs links in NEVER initializes the
 * stack pointer. Disassembling it directly confirms this -- it goes
 * straight to gp setup, BSS clearing, and __libc_init_array, then does
 * `lw a0,0(sp)` / `addi a1,sp,8` to read argc/argv OFF THE STACK before
 * calling main(). That crt0 is written for riscv-pk/spike's proxy-kernel
 * environment, where the ELF loader sets up a real process stack (argc,
 * argv, envp) and a valid sp before ever jumping to the entry point --
 * exactly like a Unix execve(). This board's boot ROM does no such
 * thing: it just sets PC to the fixed reset vector and leaves every
 * other register, sp included, in whatever undefined state the core
 * powers on with. The very first stack access in crt0 -- reading
 * `sp[0]` for argc -- is therefore reading (and, before that, our own
 * main()'s prologue `sd ra,8(sp)` is WRITING) through a garbage
 * pointer. This fully explains why DOOM starts running (confirmed via
 * the entry-point fix) but never reaches DG_DrawFrame: undefined
 * behavior from the very first function call, before Z_Init or WAD
 * loading ever get a chance to run.
 *
 * Fix: a minimal replacement _start that does exactly what crt0.o did,
 * with one line added at the very top -- set sp to a real address
 * before anything else touches it. Built with -nostartfiles so the
 * broken crt0.o is never linked in at all.
 *
 * Stack placement: 0x87000000 (112MB into DRAM), growing down. Chosen
 * from today's other finding: DRAM only reliably works up to ~120MB
 * (0x87800000); everything at/above 0x88000000 is broken. _end (the
 * heap's start, via _sbrk) sits around 0x81c0c000 (~28MB), so this
 * leaves the heap ~84MB of room to grow into before ever approaching
 * the stack from below, and the stack itself has the same ~84MB of
 * room to grow down into before it would reach the heap -- vastly more
 * than a single-threaded renderer needs either direction, with a huge
 * margin on both sides.
 */

.section .text._start
.global _start
_start:
    .option push
    .option norelax
    li      sp, 0x87000000        /* THE missing instruction */

    /* CHECKPOINT 1: sp is now valid, right after the fix. */
    li      t0, 0x86000004
    li      t1, 0xc0de0001
    sw      t1, 0(t0)

    1:
    auipc   gp, %pcrel_hi(__global_pointer$)
    addi    gp, gp, %pcrel_lo(1b)
    .option pop

    /* clear BSS: memset(&__bss_start, 0, &_end - &__bss_start) */
    la      a0, __bss_start
    la      a2, _end
    sub     a2, a2, a0
    li      a1, 0
    call    memset

    call    __libc_init_array

    /* CHECKPOINT 2: crt startup fully done, about to call main(). */
    li      t0, 0x86000008
    li      t1, 0xc0de0002
    sw      t1, 0(t0)

    li      a0, 0                 /* argc: unused, our own main() ignores it */
    li      a1, 0                 /* argv: unused, ditto */
    li      a2, 0                 /* envp */
    call    main

spin_forever:                     /* main() never returns (infinite game loop), but just in case */
    j       spin_forever
