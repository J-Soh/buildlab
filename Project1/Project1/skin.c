// memory leak checker
//#define _CRTDBG_MAP_ALLOC
//#include <stdlib.h>
//#include <crtdbg.h>

#include <stdio.h>
#include "cprocessing.h"
#include "utils.h"  // SCREEN_WIDTH/HEIGHT, gameFont, is_mouse_over, play_click, draw_cream_panel
#include "skin.h"

// Ordered so index 0 is the default blue astronaut. Names match the asset
// folders they load from, except Virtual Guy which players call the astronaut.

static const SkinDef skinDefs[SKIN_COUNT] = {
    { "Blue Astronaut", "Virtual Guy" },
    { "Mask Dude",      "Mask Dude"   },
    { "Ninja Frog",     "Ninja Frog"  },
    { "Pink Man",       "Pink Man"    },
};

//
int skinIndex = SKIN_DEFAULT;

// an array holding the loaded image for each skin's idle animation. static means it's file-private.

static CP_Image previewIdle[SKIN_COUNT] = { NULL };

// --- Picker layout (design space; panel is centered, so it scales with the window) ---
#define PICKER_PANEL_W 920.0f
#define PICKER_PANEL_H 500.0f
#define PICKER_CARD_W  190.0f
#define PICKER_CARD_H  250.0f
#define PICKER_CARD_GAP 16.0f
#define PICKER_BACK_W  220.0f
#define PICKER_BACK_H  52.0f

const SkinDef* skin_def(int index) {
	// clamp to default if out of bounds (-1 or =>10 (should never happen, but just in case)
    if (index < 0 || index >= SKIN_COUNT) index = SKIN_DEFAULT;
    return &skinDefs[index];
}

void skin_init_assets(void) {
    char path[256];
    for (int i = 0; i < SKIN_COUNT; i++) {
        if (previewIdle[i]) continue;  // already loaded (menu -> game -> menu)
        snprintf(path, sizeof(path), "Assets/Main Characters/%s/Idle (32x32).png", skinDefs[i].folder);
        previewIdle[i] = CP_Image_Load(path);
    }
}

void skin_free_assets(void) {
    for (int i = 0; i < SKIN_COUNT; i++) {
        if (previewIdle[i]) CP_Image_Free(previewIdle[i]);
        previewIdle[i] = NULL;
    }
}

CP_Image skin_preview_idle(int index) {
    if (index < 0 || index >= SKIN_COUNT) index = SKIN_DEFAULT;
    return previewIdle[index];
}

void skin_draw_preview(int index, float x, float y, float size) {
    CP_Image img = skin_preview_idle(index);
    if (!img) 
        return;
    // idle strips are 11 x 32px cells, we just need to know if it's wide enough
    if (CP_Image_GetWidth(img) < 32) 
        return;
    // Always draw the first cell (frame 0)
    CP_Image_DrawSubImage(img, x, y, size, size, 0.0f, 0.0f, 32.0f, 32.0f, 255);
}

// Shared geometry helpers so the draw and input passes always agree.
static void picker_panel(float* cx, float* cy, float* top) {
    *cx = SCREEN_WIDTH * 0.5f;
    *cy = SCREEN_HEIGHT * 0.5f;
    *top = *cy - PICKER_PANEL_H * 0.5f;
}

static void picker_card(int i, float* x, float* y) {
    float cx, cy, top;
	// picker_panel returns the center of the panel and the top edge, so we can position the cards relative to that.
    picker_panel(&cx, &cy, &top); 
	// total width of all cards + gaps, so we can center the first card and space the rest evenly
    float total = (float)SKIN_COUNT * PICKER_CARD_W + (float)(SKIN_COUNT - 1) * PICKER_CARD_GAP;
	// start x position of the first card, so we can space the rest evenly
    float start = cx - total * 0.5f;
	// position each card based on its index, with a fixed vertical offset from the top of the panel
    *x = start + (float)i * (PICKER_CARD_W + PICKER_CARD_GAP);
	//y position is fixed relative to the top of the panel, with a small offset for spacing
    *y = top + 130.0f;
}

static void picker_back(float* x, float* y) {
    float cx, cy, top;
    picker_panel(&cx, &cy, &top);
    *x = cx - PICKER_BACK_W * 0.5f;
    *y = top + 420.0f;
}

void skin_picker_draw(void) {
    float cx, cy, top;
    picker_panel(&cx, &cy, &top);

    // Dim whatever is behind (menu background or the frozen game world).
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 150));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);
    
	// Cream panel behind the cards and BACK button.
    draw_cream_panel(cx, cy, PICKER_PANEL_W, PICKER_PANEL_H);

    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255)); // Somewhat red
    CP_Settings_TextSize(50.0f);
    CP_Font_DrawText("CHOOSE YOUR SKIN", cx, top + 58.0f);

    for (int i = 0; i < SKIN_COUNT; i++) {
        float x, y;
        picker_card(i, &x, &y);
        // booleans if user has selected/is hovering over a card
        bool selected = (i == skinIndex); 
        bool hover = is_mouse_over(x, y, PICKER_CARD_W, PICKER_CARD_H);

        // Card body: selected one is warmer; hover adds a soft orange wash.
        CP_Settings_RectMode(CP_POSITION_CORNER);
        CP_Settings_Fill(selected ? CP_Color_Create(252, 236, 205, 255) : CP_Color_Create(236, 222, 202, 255)); //
        CP_Graphics_DrawRect(x, y, PICKER_CARD_W, PICKER_CARD_H);
        if (hover) {
            CP_Settings_Fill(CP_Color_Create(215, 140, 40, 70));
            CP_Graphics_DrawRect(x, y, PICKER_CARD_W, PICKER_CARD_H);
        }

        // Card border: thick red when selected, thin gold otherwise.
        CP_Settings_NoFill();
        CP_Settings_Stroke(selected ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(215, 140, 40, 255));
        CP_Settings_StrokeWeight(selected ? 6.0f : 3.0f);
        CP_Graphics_DrawRect(x, y, PICKER_CARD_W, PICKER_CARD_H);
        CP_Settings_NoStroke();

        skin_draw_preview(i, x + (PICKER_CARD_W - 96.0f) * 0.5f, y + 14.0f, 96.0f);

        CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
        CP_Settings_TextSize(24.0f);  // "Blue Astronaut" must fit inside the card
        CP_Font_DrawText(skinDefs[i].name, x + PICKER_CARD_W * 0.5f, y + 164.0f);

        if (selected) {
            CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
            CP_Settings_TextSize(22.0f);
            CP_Font_DrawText("SELECTED", x + PICKER_CARD_W * 0.5f, y + 207.0f);
        }
        else {
            CP_Settings_Fill(CP_Color_Create(120, 120, 120, 255));
            CP_Settings_TextSize(20.0f);
            CP_Font_DrawText("CLICK TO USE", x + PICKER_CARD_W * 0.5f, y + 207.0f);
        }
    }

    // BACK button
    float bx, by; 
    picker_back(&bx, &by); 
    bool backHover = is_mouse_over(bx, by, PICKER_BACK_W, PICKER_BACK_H); 

    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(backHover ? CP_Color_Create(215, 140, 40, 170): CP_Color_Create(236, 222, 202, 255));
    CP_Graphics_DrawRect(bx, by, PICKER_BACK_W, PICKER_BACK_H);
    CP_Settings_NoFill();
    CP_Settings_Stroke(CP_Color_Create(215, 140, 40, 255));
    CP_Settings_StrokeWeight(3.0f);
    CP_Graphics_DrawRect(bx, by, PICKER_BACK_W, PICKER_BACK_H);
    CP_Settings_NoStroke();

    CP_Settings_Fill(backHover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Settings_TextSize(32.0f);
    CP_Font_DrawText("BACK", cx, by + PICKER_BACK_H * 0.5f);
}

// Returns true when BACK was clicked (the caller closes the picker). Picking a
// card only updates skinIndex; it stays open so the new portrait is visible.
bool skin_picker_handle_input(void) {
    if (!CP_Input_MouseClicked()) return false;

	// BACK button: if clicked, close the picker and return true to the caller.
    float bx, by; 
    picker_back(&bx, &by); 
    if (is_mouse_over(bx, by, PICKER_BACK_W, PICKER_BACK_H)) { 
        play_click();
        return true;
    }

	// Card clicks: if the user clicks a card, update skinIndex and play a click sound.
    for (int i = 0; i < SKIN_COUNT; i++) {
        float x, y;
		// picker_card() returns the top-left corner of the card, so we can check if the mouse is over it.
        picker_card(i, &x, &y);
        if (is_mouse_over(x, y, PICKER_CARD_W, PICKER_CARD_H)) {
            if (skinIndex != i) {
                skinIndex = i;
                play_click();
            }
            return false;
        }
    }
    return false;
}
