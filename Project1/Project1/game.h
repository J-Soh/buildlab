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

// Shared with pause.c: fresh-restart helper (RESTART button). Defined in
// game.c; pause.c reaches it via this, player.c hurt_player via this too.
void restart_from_beginning(void);
// Full run reset (player + objects re-arm). Defined in game.c; hurt_player
// (player.c death path) reaches it via this.
void reset_run(void);

// Player art (defined in game.c; render_player in player.c reads them —
// loaded/freed in game_init/game_exit, so ownership stays here)
extern CP_Image playerIdle, playerRun, playerJump, playerDouble, playerFall, shadowImg;
// Plant/bullet art (defined in game.c; entity.c render reads them)
extern CP_Image plantAtkImg, bulletImg, piecesImg;
// Jump SFX (defined in game.c; player_handle_input in player.c plays it)
extern CP_Sound jumpSnd;
