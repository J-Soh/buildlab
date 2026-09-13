// ui.h
// Title screen, main menu, pause menu, transition diamond wipe, and background rendering.
// All UI globals are defined in ui.c.
#pragma once
#include "game_defs.h"

// ---- UI state ----
extern bool showSettings;     // Toggle to view instructions overlay
extern bool showQuitConfirm;  // Quit popup on the main menu (ESC -> "Are you sure...?")

// ---- Image & Font handles ----
extern CP_Image bgImage;
extern CP_Font  gameFont;
extern CP_Image playBtnImage;
extern CP_Image volBtnImage;
extern CP_Image settingsBtnImage;
extern CP_Image restartBtnImage;  // gameplay HUD, top-right

// ---- Pause menu state ----
extern bool paused;
extern int  pausePage;        // 0 = main (Resume/Restart/Settings), 1 = audio options

// ---- Transition state ----
extern CP_Image transitionImage;
extern bool     isTransitioning;
extern float    transitionTimer;

// ---- Ambient background scroll ----
extern float bgScroll;

// ---- Functions ----

// Checks if mouse position is inside a bounding box
bool is_mouse_over(float x, float y, float width, float height);

// Draw continuous scrolling brown background
void draw_background(void);

// Renders the scaling transition over screen when going from menu to gameplay (In & Out)
void draw_transition(void);

// Cream panel helper (original menu card style: shadow + cream + gold border)
void draw_cream_panel(float cx, float cy, float w, float h);

// Draws the Title Screen UI
void draw_menu_ui(void);

// Handles input during the menu screen
void handle_menu_input(void);

// Draws the Pause Menu overlay
void draw_pause_menu(void);

// Handles input during the pause screen
void handle_pause_input(void);
