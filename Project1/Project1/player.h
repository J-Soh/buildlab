// player.h
// Player character state, dust pool, physics solver, and player drawing helpers.
// All player globals are defined in player.c.
#pragma once
#include "game_defs.h"

// ---- Player instance ----
extern Player player;
extern bool   runStarted;      // true after first menu->game transition
extern bool   levelComplete;   // true when End object is reached
extern float  respawnX, respawnY; // player CENTER (checkpoint)
extern float  startX, startY;     // player CENTER (ENT_START)

// ---- Player visual assets ----
extern CP_Image playerIdle;    // 11 frames
extern CP_Image playerRun;     // 12 frames
extern CP_Image playerJump;    // 1 frame
extern CP_Image playerDouble;  // 6 frames
extern CP_Image playerFall;    // 1 frame
extern CP_Image shadowImg;
extern CP_Image dustImg;

// ---- Hearts HUD asset & state ----
extern CP_Image heartImg;      // 16x16 cells: 0 full, 1 empty
extern int      hearts;

// ---- Dust particle pool ----
extern Dust dustPool[MAX_DUST];

// ---- Functions ----

// Fresh start from the beginning: forget checkpoints, reset player + world
void restart_from_beginning(void);

// Reset player to spawn/checkpoint position & reset world entities/particles
void reset_run(void);

// Axis-separated AABB move + collide against solid map tiles
void player_physics(float dt);

// Spawn one dust particle into the dustPool
void spawn_dust(float wx, float wy, float vx, float vy, float life, float size);

// Draw one frame of an arbitrary 32x32 cell strip; facing -1 mirrors via swapped U
void draw_sprite_frame(CP_Image img, int frame, float sx, float sy, float drawSize, int facing, int alpha);

// Draw one frame of a grid sheet with cell dimensions (cellW x cellH)
void draw_sheet_frame(CP_Image img, int cellW, int cellH, int frame, float sx, float sy, float dw, float dh, int facing, int alpha);

// Standard AABB overlap test
bool aabb_overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh);

// Deal damage to player (iframe-gated)
void hurt_player(void);
