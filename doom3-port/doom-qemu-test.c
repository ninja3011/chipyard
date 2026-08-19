/* DOOM-like game loop test for QEMU
 * No I/O - just verifies game logic executes
 */

/* Minimal game state */
typedef struct {
    int frame_count;
    int x, y;
    int health;
} GameState;

GameState game;

/* Simple DOOM-like game loop */
void game_loop() {
    game.frame_count = 0;
    game.x = 160;
    game.y = 100;
    game.health = 100;

    /* Simulate 100 game frames */
    while (game.frame_count < 100) {
        /* Frame logic */
        game.frame_count++;

        /* Simple game state update */
        if (game.frame_count % 10 == 0) {
            game.x += 1;
            if (game.x > 320) game.x = 160;
        }

        /* Game logic: lose health over time (simulate enemies) */
        if (game.frame_count % 50 == 0 && game.health > 0) {
            game.health -= 10;
        }
    }
}

int main() {
    /* Initialize game */
    game.frame_count = 0;
    game.health = 100;

    /* Run game loop */
    game_loop();

    /* Verify results by exit code */
    if (game.frame_count == 100 && game.health == 80) {
        return 0;  /* Success */
    } else {
        return 1;  /* Failure */
    }
}
