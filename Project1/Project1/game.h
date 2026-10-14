#pragma once
// Gameplay state (entered from menu.c's PLAY button / SPACE).
// Game_Init runs fresh on every entry, so it starts from the beginning.

#include "cprocessing.h"

void game_init(void);
void game_update(void);
void game_exit(void);

// Game-owned render state (defined in game.c; render_map_json in extract.c
// reads them — mapImage/terrainSheet are loaded/freed in game_init/game_exit,
// camX/camY are driven by the game loop, so ownership stays here)
extern CP_Image mapImage, terrainSheet;
extern float sheetW, camX, camY;

// Shared with pause.c: fresh-restart helper (RESTART button) + footstep SFX
// (SFX +/- preview). Both defined in game.c; pause.c reaches them via these.
void restart_from_beginning(void);
extern CP_Sound stepSnd;
