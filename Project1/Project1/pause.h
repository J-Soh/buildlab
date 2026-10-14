#pragma once
// Pause menu (dim + banner + button panels, world frozen).
// (Split out of game.c so game.c keeps the game loop; the loop drives these
// via the paused/pausePage flags below.)

#include <stdbool.h>

extern bool paused;
extern int pausePage;  // 0 = main (Resume/Restart/Settings), 1 = audio options

void draw_pause_menu(void);
void handle_pause_input(void);

// Confirm popup for destructive actions (QUIT / RESTART / back-to-MENU).
// game.c's ESC routing checks/cancels through these so ESC closes the popup
// instead of unpausing underneath it.
bool pause_confirm_open(void);
void pause_cancel_confirm(void);
