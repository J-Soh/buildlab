#pragma once
// Shared canvas, assets and UI helpers used by both menu.c and game.c.
// (The reference folder's utils.c/h idea: one shared module so the two states
// don't duplicate code. Variables are defined once in utils.c.)

#include <stdbool.h>
#include "cprocessing.h"

// Fixed 1600x900 window (no scaling: UI numbers are used as-is)
extern float screenW, screenH;
#define SCREEN_WIDTH (screenW)
#define SCREEN_HEIGHT (screenH)
#define TILE_SIZE 48.0f // Scaled tile size for rendering

// Scrolling backdrop + shared font (loaded in menu_init, freed in game_exit)
#define BG_SCROLL_SPEED 70.0f
extern float bgScroll;
extern CP_Image bgImage;
extern CP_Font gameFont;

// Shared audio: volumes + menu/click sounds (world sounds stay in game.c)
extern int musicMode;  // 0 none, 1 menu, 2 world
extern float musicVol, sfxVol;
extern bool muted;
extern CP_Sound menuMusic, clickSnd, windowOpenSnd;

// Settings button art (menu settings button + in-game options button)
extern CP_Image settingsBtnImage;

// Tile solidity (map.json GIDs): shared by player/enemy/bullet physics in
// game.c AND spawn-time seating in game.c — so they live here, not extract.c.
extern int mapWidth, mapHeight;
extern int* mapData;
bool tile_is_bg_fill(int gid);
bool is_solid_cell(int tx, int ty);
bool is_solid_px(float wx, float wy);

void apply_volumes(void);
void play_click(void);
bool is_mouse_over(float x, float y, float width, float height);
void draw_background(void);
void draw_cream_panel(float cx, float cy, float w, float h);

// -----------------------------------------------------------------------------
// Player state now owned by player.h/player.c (Player struct, tuning, player).
// Enemy behavior now owned by entity.h/entity.c. Collision glue that stays
// generic (aabb, tile solidity, FX pools) remains here.
// -----------------------------------------------------------------------------
#define MAX_HEARTS 5
extern int hearts;              // HUD hearts left (MAX_HEARTS on reset)
extern bool levelComplete;      // End gate reached
extern float respawnX, respawnY; // player CENTER, world px (checkpoint; cleared on fresh start)

// Collision SFX (loaded/freed in game_init/game_exit; played from player.c / entity.c)
extern CP_Sound playerHitSnd;  // player stomps an enemy
extern CP_Sound hitSnd;        // player takes a hit
extern CP_Sound deathSnd;
extern CP_Sound coinSnd;
extern CP_Sound stepSnd;       // landing thud (also pause.c SFX preview)

// Dust particles (landing, stomp, jump): pool + spawner live here; game.c sims/draws them
#define MAX_DUST 96
typedef struct {
    bool active;
    float x, y, vx, vy, life, maxLife, size;
} Dust;
extern Dust dustPool[MAX_DUST];
void spawn_dust(float wx, float wy, float vx, float vy, float life, float size);

// Fruit pickup sparkle pool + spawner (Collected.png one-shot; game.c sims/draws it)
#define MAX_PICKUP_FX 16
#define PICKUP_FRAMES 6
#define PICKUP_FPS 18.0f
typedef struct { bool active; float x, y, t; } PickupFX;
extern PickupFX pickupFx[MAX_PICKUP_FX];
void spawn_pickup_fx(float wx, float wy);

// Generic collision + FX helpers (definitions in utils.c).
// Per-actor logic lives in player.h (player_physics, hurt_player) and
// entity.h (check_player_entity_collisions, update/draw entities + bullets).
bool aabb_overlap(float ax, float ay, float aw, float ah,
                  float bx, float by, float bw, float bh);
void draw_sprite_frame(CP_Image img, int frame, float sx, float sy, float drawSize, int facing, int alpha);
void draw_sheet_frame(CP_Image img, int cellW, int cellH, int frame, float sx, float sy, float dw, float dh, int facing, int alpha);
