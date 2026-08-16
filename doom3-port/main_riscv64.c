/*
 * DOOM on RISC-V 64-bit - Main Entry Point
 * Target: BOOM CPU on AWS FPGA / Verilator simulator
 *
 * Usage:
 *   ./doom.riscv [path_to_wad_file]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomgeneric.h"

extern void DG_Init();
extern void DG_DrawFrame();
extern void DG_SleepMs(uint32_t ms);
extern uint32_t DG_GetTicksMs();
extern int DG_GetKey(int *pressed, unsigned char *doomKey);
extern void DG_Cleanup();

int main(int argc, char *argv[]) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         DOOM - RISC-V 64-bit (RV64IMAFDv)                 ║\n");
    printf("║         Platform: BOOM CPU on AWS FPGA                    ║\n");
    printf("║         Build Date: Aug 2026                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Initialize platform (framebuffer, timers, input) */
    DG_Init();

    /* Create DOOM game instance */
    doomgeneric_Create(argc, argv);

    printf("[DOOM] Game initialized. Starting main loop...\n");
    printf("[DOOM] Press Ctrl+C to exit\n\n");

    /* Main game loop */
    uint32_t last_frame_time = 0;
    uint32_t frame_count = 0;

    while (1) {
        /* Run one game tick */
        doomgeneric_Tick();

        frame_count++;

        /* Optional: rate limiting (35 FPS target) */
        uint32_t current_time = DG_GetTicksMs();
        uint32_t frame_time = current_time - last_frame_time;

        if (frame_time < 28) {  /* 1000/35 ≈ 28.5ms per frame */
            DG_SleepMs(28 - frame_time);
        }

        last_frame_time = current_time;

        /* Periodic status output */
        if (frame_count % 350 == 0) {  /* Every ~10 seconds at 35 FPS */
            printf("[DOOM] Running: %u frames\n", frame_count);
        }
    }

    /* Cleanup (reached if game exits gracefully) */
    DG_Cleanup();

    printf("\n[DOOM] Game ended\n");

    return 0;
}

