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
    /* START GATE (see software/accel/accel_start_gated.S): uart_tsi has no
     * load-complete barrier, so spin until it writes the flag after the load. */
    li      t0, 0x86100000
    li      t1, 0x600DF00D
1:  lw      t2, 0(t0)
    bne     t2, t1, 1b
    sw      zero, 0(t0)
    fence
    .option push
    .option norelax
    li      sp, 0x87000000        /* THE missing instruction */

    /* Disable all interrupts and clear the global interrupt-enable bit.
     * Never done anywhere else in this file (confirmed by direct
     * inspection -- unlike minimal_test.S, which does this explicitly).
     * mtvec is left at whatever garbage value the core powers on with,
     * so if a periodic source (e.g. CLINT's timer) fires while it's
     * enabled, the core traps to that undefined vector. Root-caused
     * on hardware: a live capture off the JA UART showed
     * "[doom] exited with code -1" (I_Error's exit(-1), confirmed
     * against i_system.c) printing forever in a tight loop, never
     * anything else (no re-printed boot banner) and never stopping
     * even after killing the debug host tool -- i.e. not a real
     * reboot, but _exit()'s own infinite `j` self-loop (confirmed via
     * objdump) being repeatedly preempted by something and re-entering
     * a path that calls exit(-1) again. Clearing mie/mstatus.MIE here,
     * before any C code runs, removes that possibility at the source.
     */
    csrci   mstatus, 8
    csrw    mie, zero

    /* Enable the FPU (mstatus.FS, bits 14:13) before any C code runs.
     * RISC-V requires this explicitly -- with FS left at its reset
     * value of 0 (Off), ANY floating-point instruction traps as
     * illegal. This was never done here, discovered only afterward
     * while building a separate from-scratch program on this same
     * board: that program used doubles and produced total silence
     * (not even its startup banner) until this fix was added, because
     * GCC's function prologues insert callee-saved fs-register spills
     * automatically for any function using doubles -- even before the
     * function's own first line of code runs. DOOM's own build uses
     * the same -march=rv64imafdc -mabi=lp64d flags, so the compiler is
     * equally free to emit FP instructions anywhere it judges useful
     * (timing/sound code, certain library routines), regardless of
     * whether doomgeneric's classic fixed-point design "intends" to
     * avoid floating point -- not yet confirmed as the cause of the
     * unresolved crash chased for hours in this exact file's history,
     * but a real, previously-untested gap worth closing before
     * resuming that investigation.
     */
    li      t0, 0x00006000
    csrs    mstatus, t0

    /* Install a real trap handler instead of leaving mtvec at whatever
     * garbage the core powers on with. Needed because mstatus.MIE only
     * gates ASYNCHRONOUS interrupts -- a SYNCHRONOUS exception (illegal
     * instruction, misaligned access, etc.) always traps regardless of
     * MIE, so the mie/mstatus fix above does not stop the CPU from
     * jumping through an undefined mtvec if a genuine fault occurs.
     * Hardware evidence for exactly this: after that fix, checkpoints
     * 1-16 and 61-64 (startup + DG_Init) still succeed every time, but
     * checkpoints 17+ (added just past the D_DoomLoop() call site) never
     * get written at all -- execution stops somewhere between entering
     * D_DoomLoop() and its first few lines, with no crash text and no
     * repeat-loop this time, consistent with a one-time synchronous
     * fault landing in different undefined code than before (the whole
     * image shifted by 8 bytes when the mie/mstatus instructions were
     * added, changing what garbage sits at the old vector). This handler
     * records the real mcause/mepc/mtval instead of guessing further.
     */
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

/* Minimal trap handler: on ANY trap (interrupt or exception), records
 * mcause/mepc/mtval to fixed DRAM addresses so uart_tsi can read back
 * exactly what happened, then halts forever. mtvec must be 4-byte
 * aligned for direct mode (mode bits = lowest 2 bits of mtvec = 0),
 * which .align 4 guarantees. Deliberately not trying to recover/mret --
 * a bare-metal build with no real interrupt sources expected has no
 * legitimate reason to ever reach this handler; if it does, the
 * diagnostic values are more useful than an attempted (and likely
 * wrong) resume.
 */
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
