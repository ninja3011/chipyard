#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"
#include "checkpoint.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	CHECKPOINT(4); /* doomgeneric_Create entry */
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
	CHECKPOINT(5); /* DG_ScreenBuffer malloc succeeded */

	DG_Init();
	CHECKPOINT(6); /* DG_Init() done, about to call D_DoomMain() */

	D_DoomMain ();
}

