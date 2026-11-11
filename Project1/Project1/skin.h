#pragma once
// Player skins: the four characters in Assets/Main Characters. They all share
// the same 32x32 animation layout, so switching skins only swaps the image
// strips used by the player renderer.
//
// The selection is app-wide state (menu + pause + game), so it survives
// switching between states. Index 0 is Virtual Guy -- the blue astronaut --
// which makes it both the default and the fallback for invalid indices.

#include <stdbool.h>
#include "cprocessing.h"

#define SKIN_COUNT 4
#define SKIN_DEFAULT 0  // Virtual Guy = blue astronaut

typedef struct {
    const char *name;    // shown in the picker
    const char *folder;  // folder under Assets/Main Characters/
} SkinDef;

extern int skinIndex;  // 0..SKIN_COUNT-1

const SkinDef* skin_def(int index);

// Idle strips used for the picker cards and the menu button portrait.
// Loaded from menu_init and freed in game_exit (same lifetime as menu art).
void skin_init_assets(void);
void skin_free_assets(void);
CP_Image skin_preview_idle(int index);

// Draw one idle frame (32x32 cell) of a skin at x,y scaled to `size`.
void skin_draw_preview(int index, float x, float y, float size);

// Shared "Choose your skin" overlay (dim + cream panel + 4 cards + BACK).
// Both the main menu and the pause menu call these; the caller owns the
// "picker is open" flag and closes it when handle_input returns true.
void skin_picker_draw(void);
bool skin_picker_handle_input(void);
