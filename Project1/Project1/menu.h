#pragma once
// Main-menu state (called from main.c; PLAY switches to the game state).
// Button art is freed inline in game_exit at terminate (menu_exit runs on
// every menu->game switch, so it frees nothing — the menu can be revisited).

void menu_init(void);
void menu_update(void);
void menu_exit(void);

// Menu button art (defined in menu.c; game_exit frees them at terminate)
extern CP_Image playBtnImage, volBtnImage, creditBtnImage, digipenLogoImage, buildlabLogoImage;
