#pragma once
// Main-menu state (called from main.c; PLAY switches to the game state).
// Menu-owned button art is freed via menu_free_assets(), called once from
// game_exit at terminate (menu_exit runs on every menu->game switch, so it
// frees nothing — the menu can be revisited).

void menu_init(void);
void menu_update(void);
void menu_exit(void);
void menu_free_assets(void);
