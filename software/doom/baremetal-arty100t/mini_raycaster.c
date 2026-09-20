// mini_raycaster.c -- a small, from-scratch first-person raycaster for
// RocketArty100TConfig, in the spirit of DOOM/Wolfenstein3D but written
// entirely for this board: no WAD, no zone allocator, no malloc, no
// libc beyond what a handful of inline helpers below provide. Every
// byte of this program is something we wrote and can read in full,
// after a full session of chasing bugs buried in 30-year-old code we
// didn't write. Classic DDA raycasting algorithm (the same one behind
// Wolfenstein3D and, loosely, DOOM's original renderer).
//
// Video: same live ASCII-over-UART approach already proven working
// tonight -- 64x32 characters, density ramp by wall distance, streamed
// over the JA UART (real FT232 adapter, uart_puts). Input: same
// non-blocking single-byte UART polling already proven working.
// Timing: CLINT mtime, same fix already proven working (never the
// `rdtime` CSR instruction -- this core doesn't implement it).

#include <stdint.h>
#include "uart.h"

#define CLINT_MTIME_ADDR 0x0200BFF8UL
#define MTIME_HZ 50000ULL

static uint64_t rdtime(void) {
  return *(volatile uint64_t *)CLINT_MTIME_ADDR;
}

// ---------------------------------------------------------------------
// Map: 1 = wall, 0 = floor. Loosely inspired by a small DOOM-style room
// layout (a few connected chambers, not a single empty box) -- authored
// by hand for this program, not copied from any WAD.
#define MAP_W 16
#define MAP_H 16
static const char kMap[MAP_H][MAP_W + 1] = {
  "1111111111111111",
  "1000000010000001",
  "1011110010111101",
  "1010000000100001",
  "1010111111100101",
  "1010100000000101",
  "1010101111011101",
  "1000101000010001",
  "1101101011110101",
  "1000001010000101",
  "1011101010111101",
  "1010001010100001",
  "1010111110101101",
  "1000100000100001",
  "1011111111111101",
  "1111111111111111",
};

static int mapAt(int x, int y) {
  if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 1;
  return kMap[y][x] - '0';
}

// ---------------------------------------------------------------------
// Tiny math helpers -- no libm, no libc. Rotation is by fixed-angle
// increments per keypress, so sin/cos are compile-time constants, not
// runtime trig calls.
#define ABSD(x) ((x) < 0.0 ? -(x) : (x))

#define ROT_STEP 0.06                 // radians per turn keypress
#define ROT_SIN  0.05996              // sin(ROT_STEP)
#define ROT_COS  0.99820              // cos(ROT_STEP)
#define MOVE_STEP 0.14                // map-units per move keypress

// ---------------------------------------------------------------------
// Console video: same 64x32 ASCII density approach already proven live
// on this hardware tonight.
#define COLS 64
#define ROWS 32
static const char kRamp[] = " .:-=+*#%@";
#define RAMP_LEVELS ((int)(sizeof(kRamp) - 1))

typedef struct {
  double posX, posY;
  double dirX, dirY;
  double planeX, planeY; // camera plane, perpendicular to dir, controls FOV
  int health;
  int ammo;
} Player;

// Original enemy design: a simple stationary "sentry" blob, drawn as a
// billboard sprite -- not modeled on any existing game's character.
#define MAX_ENEMIES 10
typedef struct {
  double x, y;
  int health;
  int alive;
} Enemy;

// Five original placements plus five more at additional corridor
// turns/corners (all verified against kMap as open '0' floor cells),
// spreading enemies across more of the maze instead of one fixed
// five-enemy cluster.
static Enemy enemies[MAX_ENEMIES] = {
  {5.5, 3.5, 3, 1},
  {9.5, 7.5, 3, 1},
  {3.5, 11.5, 3, 1},
  {12.5, 11.5, 3, 1}, // moved from 12.5 -- that cell is a wall (kMap[12][12]=='1'), unreachable
  {7.5, 9.5, 3, 1},
  {12.5, 1.5, 3, 1},
  {14.5, 3.5, 3, 1},
  {3.5, 9.5, 3, 1},
  {6.5, 13.5, 3, 1},
  {13.5, 7.5, 3, 1},
};

// Kept separate from `enemies` (the live, mutated state) so a restart can
// cleanly restore the exact original spawn layout/health without needing
// to hardcode the same five numbers twice.
static const Enemy kInitialEnemies[MAX_ENEMIES] = {
  {5.5, 3.5, 3, 1},
  {9.5, 7.5, 3, 1},
  {3.5, 11.5, 3, 1},
  {12.5, 11.5, 3, 1}, // moved from 12.5 -- that cell is a wall (kMap[12][12]=='1'), unreachable
  {7.5, 9.5, 3, 1},
  {12.5, 1.5, 3, 1},
  {14.5, 3.5, 3, 1},
  {3.5, 9.5, 3, 1},
  {6.5, 13.5, 3, 1},
  {13.5, 7.5, 3, 1},
};

// Real win/lose state, checked once per frame -- distinct from the raw
// health/alive-count values so the freeze-on-end-state logic below has a
// single, unambiguous source of truth instead of re-deriving it in three
// different places.
typedef enum { GAME_PLAYING, GAME_WON, GAME_LOST } GameState;

static void resetGame(Player *p) {
  p->posX = 1.5; p->posY = 1.5;
  p->dirX = 1.0; p->dirY = 0.0;
  p->planeX = 0.0; p->planeY = 0.66;
  p->health = 20;
  p->ammo = 100;
  // Field-by-field, not a whole-struct assignment: this is a fully
  // freestanding (-nostdlib) build with no libc, and GCC turns a
  // struct-array copy loop like `enemies[i] = kInitialEnemies[i]` into a
  // call to memcpy(), which then fails to link since nothing provides it.
  for (int i = 0; i < MAX_ENEMIES; i++) {
    enemies[i].x = kInitialEnemies[i].x;
    enemies[i].y = kInitialEnemies[i].y;
    enemies[i].health = kInitialEnemies[i].health;
    enemies[i].alive = kInitialEnemies[i].alive;
  }
}

#define ENEMY_TOUCH_DIST 0.6
#define ENEMY_TOUCH_DAMAGE 1
#define FIRE_RANGE 8.0
#define FIRE_HALF_ANGLE_COS 0.94 // ~20 degree cone, dot-product test
#define ENEMY_CHASE_RADIUS 6.0
#define ENEMY_SPEED 0.03

// Tiny Newton-Raphson sqrt -- no libm linked in, and this converges to
// full double precision in a handful of iterations for the small
// distances (0-20ish map units) this game ever computes.
static double mySqrt(double x) {
  if (x <= 0.0) return 0.0;
  double guess = x > 1.0 ? x : 1.0;
  for (int i = 0; i < 10; i++) {
    guess = 0.5 * (guess + x / guess);
  }
  return guess;
}

// Simple chase AI: any living enemy within ENEMY_CHASE_RADIUS steps
// directly toward the player (axis-independent collision, same
// wall-sliding behavior as the player's own tryMove) -- no pathfinding,
// so a sentry can still get stuck on a corner, but within open areas it
// reads as a real threat closing in rather than a static target.
static void updateEnemies(Player *p) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].alive) continue;
    double dx = p->posX - enemies[i].x;
    double dy = p->posY - enemies[i].y;
    double distSq = dx * dx + dy * dy;
    if (distSq > ENEMY_CHASE_RADIUS * ENEMY_CHASE_RADIUS) continue;
    if (distSq < ENEMY_TOUCH_DIST * ENEMY_TOUCH_DIST) continue; // already adjacent, stop closing in further
    double dist = mySqrt(distSq);
    double moveX = (dx / dist) * ENEMY_SPEED;
    double moveY = (dy / dist) * ENEMY_SPEED;
    double nx = enemies[i].x + moveX;
    double ny = enemies[i].y + moveY;
    if (mapAt((int)nx, (int)enemies[i].y) == 0) enemies[i].x = nx;
    if (mapAt((int)enemies[i].x, (int)ny) == 0) enemies[i].y = ny;
  }
}

static void rotatePlayer(Player *p, double sinA, double cosA) {
  double oldDirX = p->dirX;
  p->dirX = p->dirX * cosA - p->dirY * sinA;
  p->dirY = oldDirX * sinA + p->dirY * cosA;
  double oldPlaneX = p->planeX;
  p->planeX = p->planeX * cosA - p->planeY * sinA;
  p->planeY = oldPlaneX * sinA + p->planeY * cosA;
}

static void tryMove(Player *p, double dx, double dy) {
  double nx = p->posX + dx;
  double ny = p->posY + dy;
  if (mapAt((int)nx, (int)p->posY) == 0) p->posX = nx;
  if (mapAt((int)p->posX, (int)ny) == 0) p->posY = ny;
}

// Any living enemy standing next to the player chips away at health --
// a simple melee-contact damage model (no pathfinding/AI yet; the
// sentries are stationary but still dangerous up close).
static uint32_t s_lastDamageTick = 0;
static void applyEnemyContactDamage(Player *p, uint64_t now) {
  if (now - s_lastDamageTick < MTIME_HZ / 2) return; // at most 2 hits/sec
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].alive) continue;
    double dx = enemies[i].x - p->posX;
    double dy = enemies[i].y - p->posY;
    if (dx * dx + dy * dy < ENEMY_TOUCH_DIST * ENEMY_TOUCH_DIST) {
      p->health -= ENEMY_TOUCH_DAMAGE;
      s_lastDamageTick = (uint32_t)now;
      break;
    }
  }
}

// Fire in the player's facing direction: project each living enemy onto
// the (facing, perpendicular) axis pair -- "along" is distance down the
// shot line, "perp" is sideways offset -- and keep the closest one that
// falls within range and inside a narrow forward cone.
// First-person weapon HUD: a fixed screen-space overlay (not a world
// sprite -- no camera transform, always drawn in the same spot) so
// there's always a constant, unambiguous "this is forward, this is you"
// reference point on screen, independent of the raycast view itself.
// 'G' (gun body) and 'Y' (muzzle flash) are both otherwise-unused glyphs
// -- not in kRamp (" .:-=+*#%@") and not the enemy glyphs ('@','o',',')
// -- so pixel_viewer.py can color them distinctly without ambiguity.
#define GUN_ROWS 8
#define GUN_COLS 14
static const char kGunShape[GUN_ROWS][GUN_COLS + 1] = {
  "      GG      ",
  "      GG      ",
  "     GGGG     ",
  "     GGGG     ",
  "    GGGGGG    ",
  "   GGGGGGGG   ",
  "   GG    GG   ",
  "   GG    GG   ",
};
static int s_muzzleFlashFrames = 0; // counts down each rendered frame after a shot

static void drawWeapon(char screen[ROWS][COLS]) {
  int startY = ROWS - GUN_ROWS;
  int startX = (COLS - GUN_COLS) / 2;
  for (int gy = 0; gy < GUN_ROWS; gy++) {
    for (int gx = 0; gx < GUN_COLS; gx++) {
      if (kGunShape[gy][gx] != 'G') continue;
      int sy = startY + gy, sx = startX + gx;
      if (sy < 0 || sy >= ROWS || sx < 0 || sx >= COLS) continue;
      screen[sy][sx] = 'G';
    }
  }
  if (s_muzzleFlashFrames > 0) {
    // A small burst just above the barrel tip -- wide on the flash's
    // first (brightest) frame, narrowing as it fades out.
    int flashY = startY - 1;
    int flashX = startX + GUN_COLS / 2;
    if (flashY >= 0) {
      screen[flashY][flashX] = 'Y';
      if (s_muzzleFlashFrames > 1) {
        if (flashX - 1 >= 0) screen[flashY][flashX - 1] = 'Y';
        if (flashX + 1 < COLS) screen[flashY][flashX + 1] = 'Y';
        if (flashY - 1 >= 0) screen[flashY - 1][flashX] = 'Y';
      }
    }
  }
}

// A scoreboard drawn INTO the character grid was tried and reverted:
// each grid cell becomes exactly one solid-colored pixel block in the
// viewer, so "HP:20" rendered as an unreadable cluster of blocks, not
// legible text -- there's no way to draw an actual letter *shape* this
// way. Real HUD text belongs in the viewer itself (a genuine font,
// genuine glyphs), parsed from the plain-text HUD line drawHud() below
// already sends -- see pixel_viewer.py's status/legend/banner labels.

static void fireWeapon(Player *p) {
  if (p->ammo <= 0) return;
  p->ammo--;
  s_muzzleFlashFrames = 2; // flashes for this frame and the next, then clears

  int best = -1;
  double bestAlong = FIRE_RANGE;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].alive) continue;
    double dx = enemies[i].x - p->posX;
    double dy = enemies[i].y - p->posY;
    double along = p->dirX * dx + p->dirY * dy;  // distance along facing direction
    double perp = -p->dirY * dx + p->dirX * dy;  // perpendicular offset
    if (along <= 0 || along > FIRE_RANGE) continue;       // behind, or out of range
    if (ABSD(perp) > along * 0.36) continue;               // ~20 degree cone (tan(20deg)~=0.36)
    if (along < bestAlong) { bestAlong = along; best = i; }
  }

  if (best >= 0) {
    enemies[best].health--;
    if (enemies[best].health <= 0) enemies[best].alive = 0;
  }
}

static char line[COLS + 3]; // + \r \n \0

static void renderFrame(Player *p) {
  // Per-column DDA raycast (Lodev-style, the standard reference
  // algorithm for this exact technique).
  static uint8_t colHeight[COLS];
  static uint8_t colChar[COLS];
  static double zBuffer[COLS]; // per-column wall distance, for sprite occlusion

  for (int x = 0; x < COLS; x++) {
    double cameraX = 2.0 * x / (double)COLS - 1.0;
    double rayDirX = p->dirX + p->planeX * cameraX;
    double rayDirY = p->dirY + p->planeY * cameraX;

    int mapX = (int)p->posX;
    int mapY = (int)p->posY;

    double deltaDistX = (rayDirX == 0.0) ? 1e30 : ABSD(1.0 / rayDirX);
    double deltaDistY = (rayDirY == 0.0) ? 1e30 : ABSD(1.0 / rayDirY);

    double sideDistX, sideDistY;
    int stepX, stepY;

    if (rayDirX < 0) { stepX = -1; sideDistX = (p->posX - mapX) * deltaDistX; }
    else             { stepX = 1;  sideDistX = (mapX + 1.0 - p->posX) * deltaDistX; }
    if (rayDirY < 0) { stepY = -1; sideDistY = (p->posY - mapY) * deltaDistY; }
    else             { stepY = 1;  sideDistY = (mapY + 1.0 - p->posY) * deltaDistY; }

    int side = 0;
    int hit = 0;
    int guard = 0;
    while (!hit && guard++ < 64) {
      if (sideDistX < sideDistY) {
        sideDistX += deltaDistX;
        mapX += stepX;
        side = 0;
      } else {
        sideDistY += deltaDistY;
        mapY += stepY;
        side = 1;
      }
      if (mapAt(mapX, mapY) > 0) hit = 1;
    }

    double perpWallDist = side == 0
      ? (sideDistX - deltaDistX)
      : (sideDistY - deltaDistY);
    if (perpWallDist < 0.05) perpWallDist = 0.05;

    int lineHeight = (int)(ROWS / perpWallDist);
    if (lineHeight > 255) lineHeight = 255;
    colHeight[x] = (uint8_t)lineHeight;

    // Density by distance: closer = denser character. Darken (one ramp
    // level down) on Y-side hits for a cheap directional-shading cue.
    int density = (int)((RAMP_LEVELS - 1) - perpWallDist * 2.2);
    if (side == 1) density -= 1;
    if (density < 0) density = 0;
    if (density > RAMP_LEVELS - 1) density = RAMP_LEVELS - 1;
    colChar[x] = (uint8_t)density;
    zBuffer[x] = perpWallDist;
  }

  // Screen buffer we can overlay sprites onto before transmitting --
  // building it in memory first (rather than streaming column-by-column
  // like the wall pass) is what makes billboard sprites practical here.
  static char screen[ROWS][COLS];
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      int drawStart = -colHeight[x] / 2 + ROWS / 2;
      int drawEnd = colHeight[x] / 2 + ROWS / 2;
      if (y < drawStart) screen[y][x] = ' ';        // ceiling
      else if (y > drawEnd) screen[y][x] = '.';     // floor
      else screen[y][x] = kRamp[colChar[x]];        // wall, shaded by distance
    }
  }

  // Billboard sprite pass: transform each enemy into camera space, then
  // draw a square block sized and shaded by distance, clipped by the
  // per-column wall z-buffer so enemies correctly hide behind walls.
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].alive) continue;
    double spriteX = enemies[i].x - p->posX;
    double spriteY = enemies[i].y - p->posY;

    // Inverse camera matrix (standard raycaster sprite transform).
    double invDet = 1.0 / (p->planeX * p->dirY - p->dirX * p->planeY);
    double transformX = invDet * (p->dirY * spriteX - p->dirX * spriteY);
    double transformY = invDet * (-p->planeY * spriteX + p->planeX * spriteY);
    if (transformY <= 0.1) continue; // behind the camera

    int spriteScreenX = (int)((COLS / 2.0) * (1.0 + transformX / transformY));
    int spriteHeight = (int)(ROWS / transformY);
    if (spriteHeight < 1) spriteHeight = 1;
    if (spriteHeight > 255) spriteHeight = 255;
    int drawStartY = -spriteHeight / 2 + ROWS / 2;
    int drawEndY = spriteHeight / 2 + ROWS / 2;
    int spriteWidth = spriteHeight; // square billboard
    int drawStartX = -spriteWidth / 2 + spriteScreenX;
    int drawEndX = spriteWidth / 2 + spriteScreenX;

    // 'X' (not '@') for full health: '@' also doubles as the densest
    // wall-shading character and, separately, the "pixel on" glyph other
    // programs on this same viewer use (the bouncing-ball animation) --
    // pixel_viewer.py deliberately does NOT tint '@' red because of that
    // ambiguity, so full-health enemies were rendering as bright
    // wall-colored blocks instead of red ones. 'X' is unused elsewhere
    // and colored red unconditionally.
    char glyph = enemies[i].health >= 3 ? 'X' : (enemies[i].health == 2 ? 'o' : ',');

    for (int sx = drawStartX; sx < drawEndX; sx++) {
      if (sx < 0 || sx >= COLS) continue;
      if (transformY >= zBuffer[sx]) continue; // occluded by a nearer wall
      for (int sy = drawStartY; sy < drawEndY; sy++) {
        if (sy < 0 || sy >= ROWS) continue;
        screen[sy][sx] = glyph;
      }
    }
  }

  drawWeapon(screen);
  if (s_muzzleFlashFrames > 0) s_muzzleFlashFrames--;

  uart_puts("\033[H");
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) line[x] = screen[y][x];
    line[COLS] = '\r';
    line[COLS + 1] = '\n';
    line[COLS + 2] = '\0';
    uart_puts(line);
  }
}

// Direct, unambiguous input-confirmation counter: incremented once per
// recognized keypress, shown in the HUD every frame. Lets a player (or
// us, testing over the live link) confirm a key was actually received
// by the board, independent of whether its visual effect is obvious.
static uint32_t s_keypressCount = 0;
static char s_lastKey = '-';

// Single source of truth for win/lose, so the input-gating in main() and
// the message drawn here can never disagree about whether the game has
// actually ended.
static GameState gameState(Player *p) {
  if (p->health <= 0) return GAME_LOST;
  int aliveCount = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) aliveCount++;
  if (aliveCount == 0) return GAME_WON;
  return GAME_PLAYING;
}

// General-purpose (no leading-zero, any digit count) integer append --
// the HP/AMMO fields below used to be hardcoded for at most 2 digits
// (a bare `if (v >= 10)` check with no further digits beyond the tens
// place), which silently truncated once ammo could reach 100: "100"
// would have printed as "00". This is the same digit-extraction loop
// already used correctly for the keypress counter below, pulled out so
// every field uses it instead of two different, inconsistently-correct
// implementations.
static void appendUint(char *buf, int *n, uint32_t value) {
  char digits[10]; int nd = 0;
  if (value == 0) digits[nd++] = '0';
  while (value > 0 && nd < 10) { digits[nd++] = '0' + (value % 10); value /= 10; }
  while (nd > 0) buf[(*n)++] = digits[--nd];
}

static void drawHud(Player *p) {
  int aliveCount = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) aliveCount++;

  char buf[COLS + 3];
  int n = 0;
  const char *hpLabel = "HP:";
  const char *ammoLabel = "  AMMO:";
  const char *enemyLabel = "  ENEMIES:";
  const char *keyLabel = "  LASTKEY:";
  const char *countLabel = " #";
  for (const char *s = hpLabel; *s; s++) buf[n++] = *s;
  appendUint(buf, &n, p->health < 0 ? 0 : (uint32_t)p->health);
  for (const char *s = ammoLabel; *s; s++) buf[n++] = *s;
  appendUint(buf, &n, p->ammo < 0 ? 0 : (uint32_t)p->ammo);
  for (const char *s = enemyLabel; *s; s++) buf[n++] = *s;
  appendUint(buf, &n, (uint32_t)aliveCount);
  for (const char *s = keyLabel; *s; s++) buf[n++] = *s;
  buf[n++] = s_lastKey;
  for (const char *s = countLabel; *s; s++) buf[n++] = *s;
  appendUint(buf, &n, s_keypressCount);
  buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = '\0';
  uart_puts(buf);

  GameState gs = gameState(p);
  if (gs == GAME_LOST) {
    uart_puts("*** YOU DIED -- press R to restart ***\r\n");
  } else if (gs == GAME_WON) {
    uart_puts("*** GAME OVER! ALL ENEMIES DEFEATED -- press R to restart ***\r\n");
  }
}

// Top-down minimap: the whole 16x16 map, one character per cell, with
// the player marked by a direction arrow and living enemies marked 'x'.
// Direct response to hardware-tested feedback that the first-person
// view alone gave no sense of "where am I" -- this is the orientation
// cue DOOM's own automap serves, done as plainly as possible here.
static void drawMinimap(Player *p) {
  char row[MAP_W + 3];
  int px = (int)p->posX;
  int py = (int)p->posY;

  char dirGlyph;
  double adx = ABSD(p->dirX), ady = ABSD(p->dirY);
  if (adx >= ady) dirGlyph = (p->dirX >= 0) ? '>' : '<';
  else            dirGlyph = (p->dirY >= 0) ? 'v' : '^';

  uart_puts("--- map ---\r\n");
  for (int y = 0; y < MAP_H; y++) {
    int n = 0;
    for (int x = 0; x < MAP_W; x++) {
      char ch;
      if (x == px && y == py) {
        ch = dirGlyph;
      } else {
        ch = '.';
        for (int i = 0; i < MAX_ENEMIES; i++) {
          if (enemies[i].alive && (int)enemies[i].x == x && (int)enemies[i].y == y) { ch = 'x'; break; }
        }
        if (ch == '.' && mapAt(x, y) != 0) ch = '#';
      }
      row[n++] = ch;
    }
    row[n++] = '\r'; row[n++] = '\n'; row[n] = '\0';
    uart_puts(row);
  }
}

int main(void) {
  uart_init();
  uart_puts("\033[2J");
  uart_puts("\r\n[mini-raycaster] arty100t bare metal -- WASD to move, no libc, no WAD\r\n");

  Player p;
  resetGame(&p); // posX/posY 1.5/1.5 is the confirmed-open floor cell (kMap[1][1] == '0')

  uint64_t lastFrame = rdtime();
  uint64_t lastEnemyUpdate = rdtime();
  const uint64_t frameInterval = MTIME_HZ / 12; // ~12 FPS target -- matches what 115200 baud can actually carry for a full 64x32 frame
  const uint64_t enemyInterval = MTIME_HZ / 20; // enemy movement steps at a fixed 20Hz, independent of render rate

  while (1) {
    int c;
    int ended = (gameState(&p) != GAME_PLAYING);
    while ((c = uart_getc_nonblock()) >= 0) {
      // 'r' always restarts, win or lose, so a demo can be reset without
      // a board reset. Every other key is ignored once the game has
      // ended -- a real "game over" freeze instead of the old behavior
      // of quietly letting you keep walking/shooting after death, which
      // read as the game not actually having ended at all.
      if ((c == 'r' || c == 'R')) {
        resetGame(&p);
        s_keypressCount++; s_lastKey = 'r';
        continue;
      }
      if (ended) { s_lastKey = '-'; continue; }
      switch (c) {
        case 'w': case 'W': tryMove(&p, p.dirX * MOVE_STEP, p.dirY * MOVE_STEP); s_keypressCount++; s_lastKey = 'w'; break;
        case 's': case 'S': tryMove(&p, -p.dirX * MOVE_STEP, -p.dirY * MOVE_STEP); s_keypressCount++; s_lastKey = 's'; break;
        case 'a': case 'A': rotatePlayer(&p, -ROT_SIN, ROT_COS); s_keypressCount++; s_lastKey = 'a'; break;
        case 'd': case 'D': rotatePlayer(&p, ROT_SIN, ROT_COS); s_keypressCount++; s_lastKey = 'd'; break;
        case ' ': case 'f': case 'F': fireWeapon(&p); s_keypressCount++; s_lastKey = 'f'; break;
        default: s_lastKey = '?'; s_keypressCount++; break; // still count unrecognized bytes, so line noise vs real keys is visible too
      }
    }

    uint64_t now = rdtime();
    if (!ended) applyEnemyContactDamage(&p, now);
    // Chase AI re-enabled now that the minimap exists: earlier hardware-
    // tested feedback was that enemies drifting on their own, with no
    // orientation cue at all, read as "the background is moving by
    // itself" rather than "a threat is approaching." With the top-down
    // map showing each 'x' visibly advancing toward the player arrow,
    // the same movement now reads as intentional and legible -- the
    // DOOM-style tension of something closing in, instead of confusion.
    if (!ended && now - lastEnemyUpdate >= enemyInterval) {
      lastEnemyUpdate = now;
      updateEnemies(&p);
    }
    if (now - lastFrame >= frameInterval) {
      lastFrame = now;
      renderFrame(&p);
      drawMinimap(&p);
      drawHud(&p);
    }
  }
  return 0;
}
