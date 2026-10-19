// memory leak checker
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include "utils.h"
#include <math.h>
#include "extract.h" // Entity/EntityDef/ENTITY_DEFS/entityPool (collision reads them)
#include "game.h"    // reset_run (hurt_player death path; defined in game.c)


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

// -----------------------------------------------------------------------------
// Shared run state + FX pools (player state itself moved to player.h/player.c;
// enemy behavior moved to entity.h/entity.c)
// -----------------------------------------------------------------------------
int hearts = MAX_HEARTS;
int maxHearts = MAX_HEARTS;
bool levelComplete = false;
float respawnX = -1.0f, respawnY = -1.0f;  // player CENTER, world px (checkpoint; cleared on fresh start)

int score = 0;
int combo = 1;
float moveSpeedMul = 1.0f;
float enemySpeedMul = 1.0f;
int bulletCost = 25;
Buffs buffs = { false, false, false };

Popup popups[MAX_POPUPS];

void spawn_popup(float wx, float wy, int amount) {
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!popups[i].active) {
            popups[i].active = true;
            popups[i].x = wx; popups[i].y = wy; popups[i].t = 0.0f;
            popups[i].amount = amount;
            return;
        }
    }
}

CP_Sound playerHitSnd = NULL;  // player stomps an enemy
CP_Sound hitSnd = NULL;        // player takes a hit
CP_Sound deathSnd = NULL;
CP_Sound coinSnd = NULL;
CP_Sound stepSnd = NULL;       // landing thud (also pause.c SFX preview)

Dust dustPool[MAX_DUST];
PickupFX pickupFx[MAX_PICKUP_FX];

void spawn_dust(float wx, float wy, float vx, float vy, float life, float size) {
    for (int i = 0; i < MAX_DUST; i++) {
        if (!dustPool[i].active) {
            dustPool[i].active = true;
            dustPool[i].x = wx; dustPool[i].y = wy;
            dustPool[i].vx = vx; dustPool[i].vy = vy;
            dustPool[i].life = dustPool[i].maxLife = life;
            dustPool[i].size = size;
            return;
        }
    }
}

void spawn_pickup_fx(float wx, float wy) {
    for (int i = 0; i < MAX_PICKUP_FX; i++) {
        if (!pickupFx[i].active) {
            pickupFx[i].active = true;
            pickupFx[i].x = wx; pickupFx[i].y = wy; pickupFx[i].t = 0.0f;
            return;
        }
    }
}

bool aabb_overlap(float ax, float ay, float aw, float ah,
                  float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// Draw one frame of an arbitrary cell strip; facing -1 mirrors via swapped U
// (generic sprite helpers used by player.c / entity.c / game.c renders)
void draw_sprite_frame(CP_Image img, int frame, float sx, float sy, float drawSize, int facing, int alpha) {
    if (!img) return;
    float u0 = (float)(frame * 32);
    float u1 = u0 + 32.0f;
    if (facing < 0) { float t = u0; u0 = u1; u1 = t; }
    CP_Image_DrawSubImage(img, sx, sy, drawSize, drawSize, u0, 0.0f, u1, 32.0f, alpha);
}

void draw_sheet_frame(CP_Image img, int cellW, int cellH, int frame, float sx, float sy, float dw, float dh, int facing, int alpha) {
    if (!img) return;
    float u0 = (float)(frame * cellW);
    float u1 = u0 + (float)cellW;
    if (facing < 0) { float t = u0; u0 = u1; u1 = t; }
    CP_Image_DrawSubImage(img, sx, sy, dw, dh, u0, 0.0f, u1, (float)cellH, alpha);
}
