#pragma once
// Gameplay state (entered from menu.c's PLAY button / SPACE).
// Game_Init runs fresh on every entry, so it starts from the beginning.

void game_init(void);
void game_update(void);
void game_exit(void);
