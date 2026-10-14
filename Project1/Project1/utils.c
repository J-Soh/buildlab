#include "utils.h"
#include <math.h>

// --- Shared canvas (fixed 1600x900 window, synced to live window) ---
float screenW = 1600.0f, screenH = 900.0f;

// --- Scrolling backdrop + shared font ---
float bgScroll = 0.0f;
CP_Image bgImage = NULL;
CP_Font gameFont = NULL;

// --- Shared audio ---
int musicMode = 0;             // 0 none, 1 menu, 2 world
float musicVol = 1.0f;
float sfxVol = 0.7f;
bool muted = false;  // menu volume-button toggle; overrides both groups
CP_Sound menuMusic = NULL, clickSnd = NULL, windowOpenSnd = NULL;

// --- Settings button art (menu settings button + in-game options button) ---
CP_Image settingsBtnImage = NULL;

// --- Tile solidity (map.json GIDs) ---
// Owned here because is_solid_px is runtime physics (player/enemy/bullet/
// shadow probes every frame), not just map-creation seating.
int mapWidth = 0;
int mapHeight = 0;
int* mapData = NULL;

// GIDs that look solid but are background fill (transparent to physics)
bool tile_is_bg_fill(int gid) {
    return gid == 24 || gid == 112;
}

// True if the tile CELL is solid (bounds-aware). gid 0 and navy fill pass through.
bool is_solid_cell(int tx, int ty) {
    if (tx < 0 || tx >= mapWidth || ty >= mapHeight) return true;  // walls + floor
    if (ty < 0 || !mapData) return false;                          // open sky
    int gid = mapData[ty * mapWidth + tx];
    if (gid <= 0 || tile_is_bg_fill(gid)) return false;
    return true;
}

bool is_solid_px(float wx, float wy) {
    int tx = (int)floorf(wx / TILE_SIZE);  // floorf: negatives map to -1, not 0
    int ty = (int)floorf(wy / TILE_SIZE);
    return is_solid_cell(tx, ty);
}

void apply_volumes(void) {
    CP_Sound_SetGroupVolume(CP_SOUND_GROUP_MUSIC, muted ? 0.0f : musicVol);
    CP_Sound_SetGroupVolume(CP_SOUND_GROUP_SFX, muted ? 0.0f : sfxVol);
}

void play_click(void) { if (clickSnd) CP_Sound_Play(clickSnd); }

// Checks if mouse position is inside an image bounding box to allow mouse cursor to click over a UI element (Play, Volume, Settings)
bool is_mouse_over(float x, float y, float width, float height) {
    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();
    return (mx >= x && mx <= x + width && my >= y && my <= y + height);
}

// Brown image across the screen, rising continuously and wrapped edge-to-edge
void draw_background(void) {
    if (!bgImage) return;

    float bgWidth = (float)CP_Image_GetWidth(bgImage);
    float bgHeight = (float)CP_Image_GetHeight(bgImage);
    if (bgWidth <= 0.0f || bgHeight <= 0.0f) return;

    // How background animation (scrolling down) works: offY increases = background image move downwards.
    // In menu.c & game.c: bgScroll += BG_SCROLL_SPEED * CP_System_GetDt(); (BG_SCROLL_SPEED = 70.0f)
    // Frame 0: bgScroll starts at 0.0f
    // Frame 1: bgScroll = 0.0f + (0.00833f * 70.0f) = 0.5831f
    // Frame 2: bgScroll = 0.5831f + (0.00833f * 70.0f) = 1.1662f
    // Frame 3: bgScroll = 1.1662f + (0.00833f * 70.0f) = 1.7493f
    // Then fmodf(bgScroll, 64) gives remainder after bgScroll is divided by 64. Then floorf() will round off this result.
    // offY = 0.0f, 1.0f, 2.0f, 3.0f ....
    float offY = floorf(fmodf(bgScroll, bgHeight));

    // put each 64x64 background image onto screen:
    // When menu/game_update call 1st time: start off at y = -64 (0-64), then next y = 0 (-64+64), then next y = 0 + 64 = 64,  next y = 64 + 64 = 128, etc...
    // when menu/game_update call 2nd time: start of at y = -63 (1-64), then next y = 1 (-63+64), then next y = 1 + 64 = 65, next y = 65 + 64 = 129, etc...
    // y = 0; y < 900 (height); y = y + 64 (background Image height)
    for (float y = offY - bgHeight; y < SCREEN_HEIGHT; y += bgHeight) {
        // x = 0; x < 1600 (width); x = x + 64 (background image width)
        for (float x = 0.0f; x < SCREEN_WIDTH; x += bgWidth) {
            // Format: void CP_Image_Draw(img, x-axis, y-axis, Image width, Image height, 255 = opaque);
            // For Image width, Image height: add +1 to fill "gap" between each image tiles (bcos their 64x64, so change it to 65x65 to fill in between gap)
            CP_Image_Draw(bgImage, x, y, bgWidth + 1.0f, bgHeight + 1.0f, 255); 
        }
    }
}

// Cream panel helper (original menu card style: shadow + cream + gold border)
void draw_cream_panel(float cx, float cy, float w, float h) {

    // Draw the initial purple shadow color rectangle.
    CP_Settings_RectMode(CP_POSITION_CENTER);
    CP_Settings_Fill(CP_Color_Create(40, 20, 50, 220));
    // Format: CP_Graphics_DrawRect(x-axis, y-axis, width, height)
    CP_Graphics_DrawRect(cx, cy, w + 16.0f, h + 16.0f);

    // Draw a smaller light beige color rectangle inside big purple rectangle
    CP_Settings_Fill(CP_Color_Create(245, 230, 210, 240)); 
    CP_Graphics_DrawRect(cx, cy, w, h);

    // Turn off the fill so the next rectangle only draws its outer edge/stroke.
    CP_Settings_NoFill();
    // Draw the gold outer edge/border.
    CP_Settings_Stroke(CP_Color_Create(215, 140, 40, 255));
    CP_Settings_StrokeWeight(6.0f);
    CP_Graphics_DrawRect(cx, cy, w - 24.0f, h - 24.0f);

    // Turn off the stroke so later shapes does not use stroke.
    CP_Settings_NoStroke();
    CP_Settings_RectMode(CP_POSITION_CORNER);
}
