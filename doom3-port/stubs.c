/* Comprehensive stubs for doomgeneric linking */

#include <stdint.h>
#include <stdio.h>

uint8_t I_VideoBuffer[320 * 200];

int usemouse = 0, mouse_acceleration = 1, mouse_threshold = 10;
int automapactive = 0, chat_on = 0, netgame = 0, usegamma = 0, consoleplayer = 0;
int key_menu_left = 0, key_menu_right = 0, key_menu_up = 0, key_menu_down = 0;

typedef struct { int x; } player_t;
player_t players[1] = {{0}};

typedef struct { void *data; } patch_t;
patch_t hu_font[256];

int iquehead = 0, iquetail = 0;

void I_BeginRead() {}
void I_EndRead() {}
void I_SetPalette(uint8_t *p) {}
uint8_t I_GetPaletteIndex(uint8_t r, uint8_t g, uint8_t b) { return 0; }
void I_Error(const char *fmt, ...) { while (1); }

int W_OpenFile(const char *f) { return 0; }
int W_Read(int h, void *b, int c) { return 0; }

int I_GetTime() { return 0; }
int I_GetTimeFast() { return 0; }

void P_SetMobjState(void *m, int s) {}
void P_MobjThinker(void *m) {}
void P_RespawnSpecials() {}
void P_SpawnMobj(int x, int y, int z, int t) {}
void P_SpawnPlayerMissile(void *p, int t) {}
void P_SpawnMapThing(void *m) {}
void S_StartSound(void *o, int s) {}
void R_SetViewSize(int size, int detail) {}

int mkdir(const char *p, int m) { return 0; }
