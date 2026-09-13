/* Temporary debug instrumentation: writes a distinct marker word to a
 * fixed DRAM scratch region at each major stage of startup, so a single
 * expensive hardware read-back (each full reload costs ~50 minutes on
 * this board's debug link) tells us exactly how far execution got,
 * instead of a single yes/no answer from whether DG_DrawFrame ever ran.
 *
 * Address: 0x86000000 (96MB into DRAM) -- comfortably inside the
 * confirmed-good <120MB range found earlier, clear of the console
 * buffer (0x84000000/64MB), the program+heap (growing up from ~28MB),
 * and the stack (starts at 0x87000000/112MB, growing down).
 *
 * Each checkpoint N writes 0xC0DE0000|N to word N (i.e. address
 * 0x86000000 + 4*N). A host reading back words 0..14 sees exactly how
 * many stages completed: the last word holding 0xC0DE00xx (rather than
 * old/random leftover data) is the last stage reached before whatever
 * happened next.
 *
 * Remove this whole file and its call sites once the real bug is found
 * -- it's diagnostic scaffolding, not part of the real port.
 */
#ifndef ARTY100T_CHECKPOINT_H
#define ARTY100T_CHECKPOINT_H

#include <stdint.h>

#define CHECKPOINT_BASE 0x86000000UL

static inline void CHECKPOINT(int n) {
  volatile uint32_t *slot = (volatile uint32_t *)(CHECKPOINT_BASE + 4UL * (uint32_t)n);
  *slot = 0xC0DE0000u | (uint32_t)(n & 0xFFFF);
}

/*
 * Checkpoint numbering (also mirrored as comments at each call site):
 *  1  doom_start.S,       right after sp is set
 *  2  doom_start.S,       right before calling main()
 *  3  main(),             entry (doomgeneric_arty100t.c)
 *  4  doomgeneric_Create, entry (doomgeneric.c)
 *  5  doomgeneric_Create, after DG_ScreenBuffer malloc succeeds
 *  6  doomgeneric_Create, after DG_Init() returns, before D_DoomMain()
 *  7  D_DoomMain,         entry (d_main.c)
 *  8  D_DoomMain,         after Z_Init() returns
 *  9  D_DoomMain,         after V_Init() returns
 * 10  D_DoomMain,         after D_FindIWAD() locates the IWAD file
 * 11  D_DoomMain,         right before D_AddFile(iwadfile) -- WAD load starts
 * 12  D_DoomMain,         right after D_AddFile(iwadfile) -- WAD load done
 * 13  D_DoomMain,         after D_IdentifyVersion()/InitGameVersion()
 * 14  D_DoomMain,         after R_Init() returns
 * 15  D_DoomMain,         after P_Init() returns
 * 16  D_DoomMain,         right before D_DoomLoop() is called
 */

#endif
