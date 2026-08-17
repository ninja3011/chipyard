/*
 * DOOM for RISC-V 64-bit (RV64IMAFDv)
 * Custom platform backend - bare-metal compatible
 *
 * Target: BOOM CPU on AWS FPGA / Verilator simulator
 * Graphics: 320x200 8-bit indexed color → memory buffer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "doomgeneric.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define DOOM_SCREEN_WIDTH   320
#define DOOM_SCREEN_HEIGHT  200
#define FB_BPP              1   // 8-bit (1 byte per pixel)
#define FB_SIZE             (DOOM_SCREEN_WIDTH * DOOM_SCREEN_HEIGHT * FB_BPP)

/* Simple timer (works on bare metal) */
#define RISC_V_MTIME_ADDR   0x0200BFF8

/* ============================================================================
 * Global State
 * ============================================================================ */

static uint8_t *fb_mem = NULL;
static uint32_t frame_count = 0;
static uint64_t start_time = 0;

/* Simple performance counter for RISC-V */
struct perf_counter {
    uint64_t frames;
    uint64_t total_time_ms;
    uint32_t min_frame_time_ms;
    uint32_t max_frame_time_ms;
};

static struct perf_counter perf = {0, 0, UINT32_MAX, 0};

/* ============================================================================
 * DOOM Generic Platform Interface
 * ============================================================================ */

void DG_Init() {
    printf("\n[DOOM] Platform initialized (RV64 bare-metal backend)\n");
    printf("[DOOM] Screen: %dx%d, FB size: %d bytes\n",
           DOOM_SCREEN_WIDTH, DOOM_SCREEN_HEIGHT, FB_SIZE);

    start_time = DG_GetTicksMs();
    perf.frames = 0;
}

void DG_DrawFrame() {
    perf.frames++;
    if (perf.frames % 35 == 0) {
        printf("[DOOM] Frames: %llu\n", perf.frames);
    }
}

void DG_SleepMs(uint32_t ms) {
    volatile uint32_t count = ms * 1000;
    while (count--);
}

uint32_t DG_GetTicksMs() {
    static uint32_t ticks = 0;
    return ++ticks;
}

/**
 * Get keyboard input
 * Returns: 1 if key available, 0 otherwise
 * doomKey: output key code
 * pressed: 1 if key pressed, 0 if released
 */
int DG_GetKey(int *pressed, unsigned char *doomKey) {
    /* TODO: Implement keyboard input from /dev/input or UART
     * For now, return no input (game runs with default behavior)
     */
    return 0;
}

void DG_Cleanup() {
    printf("[DOOM] Game ended. Frames: %llu\n", perf.frames);
}

