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
