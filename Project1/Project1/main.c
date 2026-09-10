#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.c"
#include "cprocessing.h"

// The game runs in a 1280x720 window; screenW/H track the live window size.
// Layout is authored for this 1280x720 canvas and scales via SX/SY/SF if the
// window is ever resized.
static float screenW = 1280.0f, screenH = 720.0f;
static float refW = 1280.0f, refH = 720.0f;   // reference canvas = authored 1280x720 layout size
#define SCREEN_WIDTH  (screenW)
#define SCREEN_HEIGHT (screenH)
#define TILE_SIZE 48.0f // Scaled tile size for rendering

// Project a reference-canvas value to the live window (X / Y / font).
static float SX(float v) { return v * (screenW / refW); }
static float SY(float v) { return v * (screenH / refH); }
static float SF(float v) { return v * (screenH / refH); }

// Ambient background scroll: rises continuously, wraps seamlessly (px/sec)
#define BG_SCROLL_SPEED 60.0f
static float bgScroll = 0.0f;

// State Machine for managing screens
typedef enum {
    STATE_MENU,
    STATE_GAME
} GameState;

// Global Variables
static GameState currentState = STATE_MENU;
static bool showSettings = false; // Toggle to view instructions overlay
static CP_Image bgImage = NULL;
static CP_Font gameFont = NULL;

// Button Image Handles
static CP_Image playBtnImage = NULL;
static CP_Image volBtnImage = NULL;
static CP_Image settingsBtnImage = NULL;
static CP_Image restartBtnImage = NULL;  // gameplay HUD, top-right

// Corner buttons use a design-space (48px top-right) layout, scaled live with SX/SY.

// Pause menu (PixelAdventure style: dim + banner + button panels, world frozen)
static bool paused = false;
static int pausePage = 0;  // 0 = main (Resume/Restart/Settings), 1 = audio options
static float musicVol = 0.6f;
static float sfxVol = 1.0f;
static bool muted = false;  // menu volume-button toggle; overrides both groups

static void apply_volumes(void) {
    CP_Sound_SetGroupVolume(CP_SOUND_GROUP_MUSIC, muted ? 0.0f : musicVol);
    CP_Sound_SetGroupVolume(CP_SOUND_GROUP_SFX, muted ? 0.0f : sfxVol);
}

// Transition Handles & Variables
static CP_Image transitionImage = NULL;
static bool isTransitioning = false;
static float transitionTimer = 0.0f;
static const float TRANSITION_DURATION = 0.7f; // Transition duration in seconds

// Terrain Tilesheet Handle & Dimensions
static CP_Image terrainSheet = NULL;
static float sheetW = 384.0f; // Width of Terrain (16x16).png
static float sheetH = 192.0f; // Height of Terrain (16x16).png

// Tilemap Camera (world-pixel position of the viewport's top-left corner)
static float camX = 0.0f;
static float camY = 0.0f;
static const float CAM_SPEED = 500.0f; // world px per second

// Map JSON Storage
static int mapWidth = 0;
static int mapHeight = 0;
static int tileWidth = 16;
static int tileHeight = 16;
static int* mapData = NULL;

// GIDs that look solid but are background fill (transparent to physics)
static bool tile_is_bg_fill(int gid) {
    return gid == 24 || gid == 112;
}

// -----------------------------------------------------------------------------
// Player (Virtual Guy), particles, audio
// -----------------------------------------------------------------------------
#define PLAYER_SCALE 3.0f          // 32px sprite -> 96px, matches 16->48 tiles
#define PLAYER_HB_W 36.0f          // hitbox world px
#define PLAYER_HB_H 66.0f
#define PLAYER_HB_OX 30.0f         // hitbox offset inside drawn sprite
#define PLAYER_HB_OY 30.0f
#define MOVE_SPEED 330.0f
#define SPRINT_SPEED 520.0f
#define JUMP_VEL -980.0f
#define DOUBLE_JUMP_VEL -860.0f
#define GRAVITY 2800.0f
#define MAX_FALL 1150.0f
#define COYOTE_TIME 0.10f
#define JUMP_BUFFER 0.15f

typedef struct {
    float x, y;        // hitbox top-left, world px
    float vx, vy;
    bool grounded;
    int jumps;         // 0 ground, 1 single used, 2 double used
    int facing;        // 1 right, -1 left
    float coyote, buffer;
    float animT;       // frame clock
    float stepT;       // footstep clock
    float lockT, kbx;  // hurt knockback: input locked, forced vx decays
    float invulnT;     // post-damage invulnerability timer
    bool doubleAnim;   // latched while double-jump anim plays
    float doubleAnimT;
} Player;

static Player player;
static bool runStarted = false;   // true after first menu->game transition

static CP_Image playerIdle = NULL;    // 11 frames
static CP_Image playerRun = NULL;     // 12 frames
static CP_Image playerJump = NULL;    // 1 frame
static CP_Image playerDouble = NULL;  // 6 frames
static CP_Image playerFall = NULL;    // 1 frame
static CP_Image shadowImg = NULL;
static CP_Image dustImg = NULL;

static CP_Sound menuMusic = NULL;
static CP_Sound worldMusic = NULL;
static CP_Sound stepSnd = NULL;
static CP_Sound jumpSnd = NULL;
static CP_Sound respawnSnd = NULL;    // every player spawn
static CP_Sound clickSnd = NULL;      // any button press
static CP_Sound windowOpenSnd = NULL; // a menu/panel opens

static void play_click(void) { if (clickSnd) CP_Sound_Play(clickSnd); }


static int musicMode = 0;             // 0 none, 1 menu, 2 world

// Hearts HUD
#define MAX_HEARTS 5
static CP_Image heartImg = NULL;  // 16x16 cells: 0 full, 1 empty
static int hearts = MAX_HEARTS;

#define MAX_DUST 96
typedef struct {
    bool active;
    float x, y, vx, vy, life, maxLife, size;
} Dust;
static Dust dustPool[MAX_DUST];

// -----------------------------------------------------------------------------
// Map object extraction: checkpoints, enemies, traps (map.json objectgroups)
// -----------------------------------------------------------------------------
#define MAX_ENTITIES 72
typedef enum {
    ENT_SAW, ENT_SPIKES,
    ENT_PLANT, ENT_CHICKEN, ENT_BAT, ENT_RINO,
    ENT_START, ENT_CHECKPOINT, ENT_END,
    ENT_FRUIT_ORANGE, ENT_FRUIT_CHERRIES, ENT_FRUIT_KIWI, ENT_FRUIT_MELON, ENT_FRUIT_STRAWBERRY,
    ENT_COUNT
} EntityKind;

typedef struct {
    const char* path;
    const char* altPath;   // flattened / activated variant sheet (checkpoint flag)
    int cellW, cellH;      // frame cell size on the sheet (source px)
    float hbW, hbH;        // hitbox as a fraction of the scaled cell
    bool hazard;           // non-living damager (saw, spikes)
    bool staticBody;       // no patrol / no movement
    bool flies;            // bat: no gravity, hovers
    float speed;           // patrol speed (world px/s); 0 = stationary
    float animFps;         // animation playback speed
} EntityDef;

static const EntityDef ENTITY_DEFS[ENT_COUNT] = {
    { "Assets/Traps/Saw/On (38x38).png",        NULL,              38, 38, 0.55f, 0.55f, true,  false, false,   0.0f, 12.0f },
    { "Assets/Traps/Spikes/Idle.png",           NULL,              16, 16, 0.70f, 0.45f, true,  false, false,   0.0f,  1.0f },
    { "Assets/Enemies/Plant/Idle (44x42).png",  NULL,              44, 42, 0.60f, 0.60f, false, true,  false,   0.0f,  8.0f },
    { "Assets/Enemies/Chicken/Run (32x34).png", NULL,              32, 34, 0.68f, 0.68f, false, false, false,  90.0f, 10.0f },
    { "Assets/Enemies/Bat/Flying (46x30).png",  NULL,              46, 30, 0.60f, 0.60f, false, true,  true,   30.0f,  8.0f },
    { "Assets/Enemies/Rino/Run (52x34).png",    NULL,              52, 34, 0.68f, 0.68f, false, false, false, 130.0f,  8.0f },
    { "Assets/Items/Checkpoints/Start/Start (Idle).png", NULL,      64, 64, 0.55f, 0.55f, false, true,  false,   0.0f,  1.0f },
    { "Assets/Items/Checkpoints/Checkpoint/Checkpoint (No Flag).png",
      "Assets/Items/Checkpoints/Checkpoint/Checkpoint (Flag Idle)(64x64).png",
      64, 64, 0.55f, 0.55f, false, true, false, 0.0f, 1.0f },
    { "Assets/Items/Checkpoints/End/End (Idle).png", NULL,         64, 64, 0.55f, 0.55f, false, true,  false,   0.0f,  1.0f },
    { "Assets/Items/Fruits/Orange.png",     NULL, 32, 32, 0.65f, 0.65f, false, true,  false,   0.0f, 12.0f },
    { "Assets/Items/Fruits/Cherries.png",   NULL, 32, 32, 0.65f, 0.65f, false, true,  false,   0.0f, 12.0f },
    { "Assets/Items/Fruits/Kiwi.png",       NULL, 32, 32, 0.65f, 0.65f, false, true,  false,   0.0f, 12.0f },
    { "Assets/Items/Fruits/Melon.png",      NULL, 32, 32, 0.65f, 0.65f, false, true,  false,   0.0f, 12.0f },
    { "Assets/Items/Fruits/Strawberry.png", NULL, 32, 32, 0.65f, 0.65f, false, true,  false,   0.0f, 12.0f },
};

typedef struct {
    EntityKind kind;
    bool active;
    CP_Image baseImage, altImage;
    int totalFrames, cellW, cellH;
    float x, y;            // world px, CENTER of the entity
    float sx, sy;          // spawn point (restored on reset)
    float baseY;           // bat hover baseline
    float w, h;            // world-px hitbox, centered on (x, y)
    float vx, vy;
    int facing;
    float animT, hoverT;
    float atkT, coolT;     // plant shooter: attack clock + cooldown
    bool attacking, fired; // attack anim running / bullet released this cycle
    bool grounded;
    bool activated;        // checkpoint reached -> flag out
} Entity;

static Entity entityPool[MAX_ENTITIES];
static int entityCount = 0;
static float entityScale = 3.0f;   // world px per Tiled px == TILE_SIZE / tileWidth

static bool levelComplete = false;
static float respawnX = -1.0f, respawnY = -1.0f;  // player CENTER, world px

static CP_Sound playerHitSnd = NULL;  // player stomps an enemy
static CP_Sound hitSnd = NULL;        // player takes a hit
static CP_Sound deathSnd = NULL;
static CP_Sound coinSnd = NULL;

// Plant shooter shared art + projectile pools
static CP_Image plantAtkImg = NULL;   // Attack (44x42), 8 frames
static CP_Image bulletImg = NULL;     // Bullet 16x16
static CP_Image piecesImg = NULL;     // Bullet Pieces 32x16, 2 frames (impact)
#define MAX_BULLETS 12
typedef struct { bool active; float x, y, vx, vy, life; } Bullet;
static Bullet bullets[MAX_BULLETS];
#define MAX_BULLET_FX 8
typedef struct { bool active; float x, y, t; } BulletFX;
static BulletFX bulletFx[MAX_BULLET_FX];
#define BULLET_SPEED 420.0f


// -----------------------------------------------------------------------------
// Helper Functions ("Methods")
// -----------------------------------------------------------------------------

// Checks if mouse position is inside an image bounding box to allow mouse cursor to click over a UI element (Play, Volume, Settings)
static bool is_mouse_over(float x, float y, float width, float height) {
    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();
    return (mx >= x && mx <= x + width && my >= y && my <= y + height);
}

// Brown image across the screen, rising continuously and wrapped edge-to-edge
static void draw_background(void) {
    if (!bgImage) return;

    float bgWidth = (float)CP_Image_GetWidth(bgImage);
    float bgHeight = (float)CP_Image_GetHeight(bgImage);
    if (bgWidth <= 0.0f || bgHeight <= 0.0f) return;

    // Rows slide down by offY over time, so the pattern appears to rise; wraps with no gap.
    // offY is snapped to whole pixels: fractional rows land tiles on fractional
    // screen pixels, opening 1px cracks (the horizontal "lines"). +1px overlap
    // seals any crack the display scaling itself might add (edges match, so invisible).
    float offY = floorf(fmodf(bgScroll, bgHeight));
    for (float y = offY - bgHeight; y < SCREEN_HEIGHT; y += bgHeight) {
        for (float x = 0.0f; x < SCREEN_WIDTH; x += bgWidth) {
            CP_Image_Draw(bgImage, x, y, bgWidth + 1.0f, bgHeight + 1.0f, 255);
        }
    }
}

// True if the tile CELL is solid (bounds-aware). gid 0 and navy fill pass through.
static bool is_solid_cell(int tx, int ty) {
    if (tx < 0 || tx >= mapWidth || ty >= mapHeight) return true;  // walls + floor
    if (ty < 0 || !mapData) return false;                          // open sky
    int gid = mapData[ty * mapWidth + tx];
    if (gid <= 0 || tile_is_bg_fill(gid)) return false;
    return true;
}

static bool is_solid_px(float wx, float wy) {
    int tx = (int)floorf(wx / TILE_SIZE);  // floorf: negatives map to -1, not 0
    int ty = (int)floorf(wy / TILE_SIZE);
    return is_solid_cell(tx, ty);
}

// Leftmost lowest open pocket with headroom + floor: adapts to map edits
static void find_spawn_cell(int* outTx, int* outTy) {
    if (!mapData || mapWidth <= 0 || mapHeight <= 0) { *outTx = 0; *outTy = 0; return; }
    for (int tx = 0; tx < mapWidth; tx++) {
        for (int ty = mapHeight - 1; ty >= 1; ty--) {
            int i = ty * mapWidth + tx;
            if (mapData[i] == 0 && mapData[(ty - 1) * mapWidth + tx] == 0 && is_solid_cell(tx, ty + 1)) {
                *outTx = tx; *outTy = ty;
                return;
            }
        }
    }
    *outTx = mapWidth / 2; *outTy = mapHeight / 2;
}

static void reset_run(void) {
    if (respawnX > 0.0f && respawnY > 0.0f) {
        player.x = roundf(respawnX - PLAYER_HB_W * 0.5f);
        player.y = roundf(respawnY - PLAYER_HB_H * 0.5f);
    } else {
        int tx, ty;
        find_spawn_cell(&tx, &ty);
        player.x = tx * TILE_SIZE + (TILE_SIZE - PLAYER_HB_W) * 0.5f;
        player.y = (ty + 1) * TILE_SIZE - PLAYER_HB_H - 1.0f;
    }
    player.vx = 0.0f; player.vy = 0.0f;
    player.grounded = false; player.jumps = 0; player.facing = 1;
    player.coyote = 0.0f; player.buffer = 0.0f;
    player.animT = 0.0f; player.stepT = 0.0f;
    player.doubleAnim = false; player.doubleAnimT = 0.0f;
    player.lockT = 0.0f; player.kbx = 0.0f;
    player.invulnT = 0.0f;
    hearts = MAX_HEARTS;
    levelComplete = false;
    for (int i = 0; i < MAX_DUST; i++) dustPool[i].active = false;
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false;
    for (int i = 0; i < MAX_BULLET_FX; i++) bulletFx[i].active = false;

    // Re-arm every extracted map object at its spawn point (death / restart)
    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        const EntityDef* def = &ENTITY_DEFS[e->kind];
        e->active = true;
        e->x = e->sx; e->y = e->sy;
        e->baseY = e->sy;
        e->vx = (def->speed > 0.0f) ? (((i & 1) == 0) ? -def->speed : def->speed) : 0.0f;
        e->vy = 0.0f;
        e->facing = e->vx >= 0.0f ? 1 : -1;
        e->grounded = false;
        e->activated = false;
        e->animT = 0.0f; e->hoverT = 0.0f;
        e->atkT = 0.0f; e->coolT = 0.0f; e->attacking = false; e->fired = false;
    }

    if (respawnSnd) CP_Sound_Play(respawnSnd);
    camX = player.x + PLAYER_HB_W * 0.5f - SCREEN_WIDTH * 0.5f;
    camY = player.y + PLAYER_HB_H * 0.5f - SCREEN_HEIGHT * 0.5f;
}

// Axis-separated AABB move + collide against solid tiles
static void player_physics(float dt) {
    // --- X axis ---
    player.x += player.vx * dt;
    if (player.vx > 0.0f) {
        float ex = player.x + PLAYER_HB_W;
        if (is_solid_px(ex, player.y + 2.0f) || is_solid_px(ex, player.y + PLAYER_HB_H * 0.5f) || is_solid_px(ex, player.y + PLAYER_HB_H - 2.0f)) {
            player.x = ((int)(ex / TILE_SIZE)) * TILE_SIZE - PLAYER_HB_W - 0.01f;
            player.vx = 0.0f;
        }
    }
    else if (player.vx < 0.0f) {
        if (is_solid_px(player.x, player.y + 2.0f) || is_solid_px(player.x, player.y + PLAYER_HB_H * 0.5f) || is_solid_px(player.x, player.y + PLAYER_HB_H - 2.0f)) {
            player.x = (((int)floorf(player.x / TILE_SIZE)) + 1) * TILE_SIZE + 0.01f;
            player.vx = 0.0f;
        }
    }

    // --- Y axis ---
    bool wasGrounded = player.grounded;
    float fallSpeed = player.vy;
    player.y += player.vy * dt;
    player.grounded = false;
    if (player.vy >= 0.0f) {
        float ey = player.y + PLAYER_HB_H;
        bool hit = is_solid_px(player.x + 2.0f, ey) || is_solid_px(player.x + PLAYER_HB_W - 2.0f, ey);
        if (hit) {
            player.y = ((int)(ey / TILE_SIZE)) * TILE_SIZE - PLAYER_HB_H - 0.01f;
            player.vy = 0.0f;
            player.grounded = true;
            player.jumps = 0;
            player.doubleAnim = false;
        }
    }
    else {
        if (is_solid_px(player.x + 2.0f, player.y) || is_solid_px(player.x + PLAYER_HB_W - 2.0f, player.y)) {
            player.y = (((int)floorf(player.y / TILE_SIZE)) + 1) * TILE_SIZE + 0.01f;
            player.vy = 0.0f;
        }
    }
    if (player.grounded && !wasGrounded && fallSpeed > 500.0f && stepSnd) {
        CP_Sound_Play(stepSnd);  // landing thud reuses footstep
        for (int k = 0; k < 6; k++) {
            for (int i = 0; i < MAX_DUST; i++) {
                if (!dustPool[i].active) {
                    dustPool[i].active = true;
                    dustPool[i].x = player.x + PLAYER_HB_W * 0.5f + (float)((k % 3) - 1) * 14.0f;
                    dustPool[i].y = player.y + PLAYER_HB_H - 4.0f;
                    dustPool[i].vx = (float)((k % 3) - 1) * 90.0f;
                    dustPool[i].vy = -60.0f - (float)(k / 3) * 60.0f;
                    dustPool[i].life = dustPool[i].maxLife = 0.45f;
                    dustPool[i].size = 20.0f;
                    break;
                }
            }
        }
    }
}

static void spawn_dust(float wx, float wy, float vx, float vy, float life, float size) {
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

// Draw one frame of an arbitrary cell strip; facing -1 mirrors via swapped U
static void draw_sprite_frame(CP_Image img, int frame, float sx, float sy, float drawSize, int facing, int alpha) {
    if (!img) return;
    float u0 = (float)(frame * 32);
    float u1 = u0 + 32.0f;
    if (facing < 0) { float t = u0; u0 = u1; u1 = t; }
    CP_Image_DrawSubImage(img, sx, sy, drawSize, drawSize, u0, 0.0f, u1, 32.0f, alpha);
}

static void draw_sheet_frame(CP_Image img, int cellW, int cellH, int frame, float sx, float sy, float dw, float dh, int facing, int alpha) {
    if (!img) return;
    float u0 = (float)(frame * cellW);
    float u1 = u0 + (float)cellW;
    if (facing < 0) { float t = u0; u0 = u1; u1 = t; }
    CP_Image_DrawSubImage(img, sx, sy, dw, dh, u0, 0.0f, u1, (float)cellH, alpha);
}

// Map the Tiled object layer name to an EntityKind (mirrors fruits "extract orange")
static EntityKind kind_from_layer_name(const char* name) {
    if (!name) return ENT_COUNT;
    if (strstr(name, "Saw"))         return ENT_SAW;
    if (strstr(name, "Spikes"))      return ENT_SPIKES;
    if (strstr(name, "Plant"))       return ENT_PLANT;
    if (strstr(name, "Chicken"))     return ENT_CHICKEN;
    if (strstr(name, "Bat"))         return ENT_BAT;
    if (strstr(name, "Rino"))        return ENT_RINO;
    if (strstr(name, "Start"))       return ENT_START;
    if (strstr(name, "Checkpoint"))  return ENT_CHECKPOINT;
    if (strstr(name, "End"))         return ENT_END;
    if (strstr(name, "Orange"))     return ENT_FRUIT_ORANGE;
    if (strstr(name, "Cherries"))   return ENT_FRUIT_CHERRIES;
    if (strstr(name, "Kiwi"))       return ENT_FRUIT_KIWI;
    if (strstr(name, "Melon"))      return ENT_FRUIT_MELON;
    if (strstr(name, "Strawberry")) return ENT_FRUIT_STRAWBERRY;
    return ENT_COUNT;
}

// Instantiate one map-placed object at world coords (map px * entityScale)
static void spawn_entity(EntityKind kind, float wx, float wy) {
    if (entityCount >= MAX_ENTITIES || kind >= ENT_COUNT) return;

    const EntityDef* def = &ENTITY_DEFS[kind];
    Entity* e = &entityPool[entityCount++];
    e->kind = kind;
    e->active = true;
    e->baseImage = def->path ? CP_Image_Load(def->path) : NULL;
    e->altImage = def->altPath ? CP_Image_Load(def->altPath) : NULL;
    e->cellW = def->cellW;
    e->cellH = def->cellH;
    e->totalFrames = 1;
    if (e->baseImage) {
        int iw = CP_Image_GetWidth(e->baseImage);
        if (iw > def->cellW) e->totalFrames = iw / def->cellW;
    }
    e->x = wx; e->y = wy;
    e->sx = wx; e->sy = wy;
    e->baseY = wy;
    e->w = (float)def->cellW * entityScale * def->hbW;
    e->h = (float)def->cellH * entityScale * def->hbH;
    e->vx = (def->speed > 0.0f) ? (((entityCount & 1) == 0) ? -def->speed : def->speed) : 0.0f;
    e->vy = 0.0f;
    e->facing = e->vx >= 0.0f ? 1 : -1;
    e->animT = 0.0f; e->hoverT = 0.0f;
    e->grounded = false;
    e->activated = false;
}

// Extract the objects of one objectgroup into the entity pool
static void parse_object_group_entities(cJSON* layer) {
    cJSON* nameItem = cJSON_GetObjectItemCaseSensitive(layer, "name");
    const char* name = (cJSON_IsString(nameItem) && nameItem->valuestring) ? nameItem->valuestring : "";
    EntityKind kind = kind_from_layer_name(name);
    if (kind >= ENT_COUNT) return;   // not a kind we manage (fruits etc.)

    cJSON* objects = cJSON_GetObjectItemCaseSensitive(layer, "objects");
    if (!cJSON_IsArray(objects)) return;

    int n = cJSON_GetArraySize(objects);
    for (int i = 0; i < n; i++) {
        cJSON* obj = cJSON_GetArrayItem(objects, i);
        if (!obj) continue;
        cJSON* x = cJSON_GetObjectItemCaseSensitive(obj, "x");
        cJSON* y = cJSON_GetObjectItemCaseSensitive(obj, "y");
        if (cJSON_IsNumber(x) && cJSON_IsNumber(y)) {
            spawn_entity(kind, (float)x->valuedouble * entityScale, (float)y->valuedouble * entityScale);
        }
    }
}

// Recurse through the layer tree: read the tile layer + all objectgroups
static void process_layers_world(cJSON* layers) {
    if (!cJSON_IsArray(layers)) return;

    int n = cJSON_GetArraySize(layers);
    for (int l = 0; l < n; l++) {
        cJSON* layer = cJSON_GetArrayItem(layers, l);
        if (!layer) continue;

        cJSON* type = cJSON_GetObjectItemCaseSensitive(layer, "type");
        if (!cJSON_IsString(type) || !type->valuestring) continue;

        if (strcmp(type->valuestring, "tilelayer") == 0) {
            cJSON* data = cJSON_GetObjectItemCaseSensitive(layer, "data");
            if (cJSON_IsArray(data)) {
                int dataSize = cJSON_GetArraySize(data);
                if (mapData) free(mapData);
                mapData = (int*)malloc(sizeof(int) * dataSize);
                for (int i = 0; i < dataSize; i++) {
                    cJSON* tile = cJSON_GetArrayItem(data, i);
                    if (cJSON_IsNumber(tile)) mapData[i] = tile->valueint;
                }
            }
        }
        else if (strcmp(type->valuestring, "objectgroup") == 0) {
            parse_object_group_entities(layer);
        }
        else if (strcmp(type->valuestring, "group") == 0) {
            cJSON* sub = cJSON_GetObjectItemCaseSensitive(layer, "layers");
            if (cJSON_IsArray(sub)) process_layers_world(sub);
        }
    }
}

// Seeds fly straight, pop into pieces on tiles, hurt on touch (iframe-gated)
static void hurt_player(void);  // fwd: defined with collisions below
static void update_bullets(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet* b = &bullets[i];
        if (!b->active) continue;
        b->life -= dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        bool dead = (b->life <= 0.0f);
        if (!dead && is_solid_px(b->x, b->y)) dead = true;  // wall impact
        if (!dead && b->x > player.x - 14.0f && b->x < player.x + PLAYER_HB_W + 14.0f &&
            b->y > player.y - 14.0f && b->y < player.y + PLAYER_HB_H + 14.0f) {
            hurt_player();
            dead = true;
        }
        if (dead) {
            b->active = false;
            for (int f = 0; f < MAX_BULLET_FX; f++) {
                if (!bulletFx[f].active) {
                    bulletFx[f].active = true;
                    bulletFx[f].x = b->x; bulletFx[f].y = b->y; bulletFx[f].t = 0.0f;
                    break;
                }
            }
        }
    }
    for (int f = 0; f < MAX_BULLET_FX; f++) {
        if (bulletFx[f].active) {
            bulletFx[f].t += dt;
            if (bulletFx[f].t >= 0.3f) bulletFx[f].active = false;
        }
    }
}

static void draw_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active || !bulletImg) continue;
        float sx = roundf(bullets[i].x - 20.0f - camX);
        float sy = roundf(bullets[i].y - 20.0f - camY);
        if (sx < -60.0f || sx > SCREEN_WIDTH + 60.0f || sy < -60.0f || sy > SCREEN_HEIGHT + 60.0f) continue;
        CP_Image_Draw(bulletImg, sx, sy, 40.0f, 40.0f, 255);
    }
    for (int f = 0; f < MAX_BULLET_FX; f++) {
        if (!bulletFx[f].active || !piecesImg) continue;
        int fr = (bulletFx[f].t > 0.15f) ? 1 : 0;
        draw_sheet_frame(piecesImg, 16, 16, fr, roundf(bulletFx[f].x - 20.0f - camX), roundf(bulletFx[f].y - 20.0f - camY), 40.0f, 40.0f, 1, 255);
    }
}

#define ENT_GRAVITY 2400.0f
#define ENT_MAX_FALL 950.0f

// Move every living object (gravity + patrol; bats hover; traps stay put)
static void update_entities(float dt) {
    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        e->animT += dt;

        if (def->flies) {
            e->hoverT += dt;
            e->y = e->baseY + sinf(e->hoverT * 2.2f) * 16.0f;  // gentle bob
            continue;
        }

        // Plant shooter: faces the player, plays Attack, fires one seed per cycle
        if (e->kind == ENT_PLANT) {
            float pcx = player.x + PLAYER_HB_W * 0.5f;
            float pcy = player.y + PLAYER_HB_H * 0.5f;
            float dx = pcx - e->x, dy = pcy - e->y;
            e->facing = (dx >= 0.0f) ? 1 : -1;
            e->coolT -= dt;
            if (e->attacking) {
                e->atkT += dt;
                if (!e->fired && e->atkT >= 0.35f) {
                    e->fired = true;
                    float len = sqrtf(dx * dx + dy * dy);
                    if (len > 1.0f) {
                        for (int b = 0; b < MAX_BULLETS; b++) {
                            if (!bullets[b].active) {
                                bullets[b].active = true;
                                bullets[b].x = e->x;
                                bullets[b].y = e->y - e->h * 0.2f;
                                bullets[b].vx = dx / len * BULLET_SPEED;
                                bullets[b].vy = dy / len * BULLET_SPEED;
                                bullets[b].life = 3.0f;
                                break;
                            }
                        }
                    }
                }
                if (e->atkT >= 0.67f) { e->attacking = false; e->coolT = 1.6f; }
            }
            else if (e->coolT <= 0.0f && !levelComplete &&
                     fabsf(dx) < 560.0f && fabsf(dy) < 220.0f) {
                e->attacking = true;
                e->atkT = 0.0f;
                e->fired = false;
            }
            continue;
        }

        bool isFruit = e->kind >= ENT_FRUIT_ORANGE && e->kind <= ENT_FRUIT_STRAWBERRY;
        bool physicsBody = !def->hazard && !isFruit
            && e->kind != ENT_START && e->kind != ENT_CHECKPOINT && e->kind != ENT_END;
        if (!physicsBody) continue;

        // Gravity + landing

        // Gravity + landing
        e->vy += ENT_GRAVITY * dt;
        if (e->vy > ENT_MAX_FALL) e->vy = ENT_MAX_FALL;
        e->y += e->vy * dt;
        float halfH = e->h * 0.5f;
        float bottom = e->y + halfH;
        if (is_solid_px(e->x - e->w * 0.35f, bottom) || is_solid_px(e->x + e->w * 0.35f, bottom)) {
            e->y = ((int)(bottom / TILE_SIZE)) * TILE_SIZE - halfH - 0.01f;
            e->vy = 0.0f;
            e->grounded = true;
        } else {
            e->grounded = false;
        }

        // Patrol: walk, reverse at walls and ledges
        if (e->grounded && def->speed > 0.0f) {
            e->x += e->vx * dt;
            float fx = (e->vx > 0.0f) ? e->x + e->w * 0.5f : e->x - e->w * 0.5f;
            bool wall = is_solid_px(fx, e->y - halfH * 0.6f) || is_solid_px(fx, e->y + 2.0f);
            float ahead = e->x + ((e->vx > 0.0f) ? (e->w * 0.5f + 4.0f) : -(e->w * 0.5f + 4.0f));
            bool ledge = !is_solid_px(ahead, e->y + halfH + 6.0f);
            if (wall || ledge) {
                e->vx = -e->vx;
                e->facing = e->vx > 0.0f ? 1 : -1;
            }
        }
    }
}

static bool aabb_overlap(float ax, float ay, float aw, float ah,
                         float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// Deal damage to the player (iframe-gated) from an enemy/trap contact
static void hurt_player(void) {
    if (player.invulnT > 0.0f || levelComplete) return;

    // Traps and enemies consume ONE heart (never an instant kill)
    hearts--;
    if (hitSnd) CP_Sound_Play(hitSnd);   // player got hit
    player.invulnT = 1.4f;
    player.lockT = 0.5f;
    player.kbx = -300.0f * (float)player.facing;
    if (player.vy > -200.0f) player.vy = -300.0f;  // hop up + knock back

    if (hearts <= 0) {
        hearts = 0;
        if (deathSnd) CP_Sound_Play(deathSnd);
        reset_run();
        player.invulnT = 1.2f;
    }
}

// Player vs map objects: engage checkpoints, stomp enemies, take damage
static void check_player_entity_collisions(void) {
    float px = player.x, py = player.y, pw = PLAYER_HB_W, ph = PLAYER_HB_H;

    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        float hx = e->x - e->w * 0.5f, hy = e->y - e->h * 0.5f;

        // Checkpoint / End gates trigger regardless of iframes
        if (e->kind == ENT_CHECKPOINT || e->kind == ENT_END) {
            if (aabb_overlap(px, py, pw, ph, hx, hy, e->w, e->h)) {
                if (e->kind == ENT_END) {
                    levelComplete = true;
                } else if (!e->activated) {
                    e->activated = true;
                    respawnX = e->x;
                    respawnY = e->y;
                    if (coinSnd) CP_Sound_Play(coinSnd);
                }
            }
            continue;
        }

        if (e->kind == ENT_START) continue;  // spawn marker only

        // Fruits: collect on touch
        if (e->kind >= ENT_FRUIT_ORANGE && e->kind <= ENT_FRUIT_STRAWBERRY) {
            if (aabb_overlap(px, py, pw, ph, hx, hy, e->w, e->h)) {
                e->active = false;
                if (coinSnd) CP_Sound_Play(coinSnd);
            }
            continue;
        }

        if (player.invulnT > 0.0f) continue;
        if (!aabb_overlap(px, py, pw, ph, hx, hy, e->w, e->h)) continue;

        if (def->hazard) {
            hurt_player();
            continue;
        }

        // Enemy: bounce on the head, otherwise take damage
        float playerBottom = py + ph;
        float entTop = e->y - e->h * 0.5f;
        if (player.vy > 80.0f && playerBottom <= entTop + 16.0f) {
            e->active = false;
            if (playerHitSnd) CP_Sound_Play(playerHitSnd);  // player stomped an enemy
            for (int k = 0; k < 5; k++)
                spawn_dust(e->x + (float)((k % 3) - 1) * 14.0f, e->y - e->h * 0.2f,
                           (float)((k % 3) - 1) * 90.0f, -140.0f, 0.45f, 18.0f);
            player.vy = -470.0f;
            player.jumps = 1;
            player.doubleAnim = false;
        } else {
            hurt_player();
        }
    }
}

// Draw all extracted map objects at their world positions (camera-relative)
static void render_entities(void) {
    CP_Settings_ImageMode(CP_POSITION_CORNER);

    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        CP_Image img = e->baseImage;
        if (e->kind == ENT_CHECKPOINT && e->activated && e->altImage) img = e->altImage;
        bool plantFiring = (e->kind == ENT_PLANT && e->attacking && plantAtkImg);
        if (plantFiring) img = plantAtkImg;
        if (!img) continue;

        int frames = e->totalFrames;
        if (e->kind == ENT_CHECKPOINT)
            frames = e->activated ? (CP_Image_GetWidth(img) / e->cellW) : 1;
        if (plantFiring) frames = 8;  // Attack strip
        if (frames < 1) frames = 1;

        float animFps = (e->kind == ENT_CHECKPOINT && e->activated) ? 10.0f : def->animFps;
        if (plantFiring) animFps = 12.0f;
        int frame = (frames > 1) ? (((int)(e->animT * animFps)) % frames) : 0;
        if (plantFiring) { frame = (int)(e->atkT * 12.0f); if (frame > 7) frame = 7; }  // synced to release

        // Fruits are drawn at the tile footprint, like their map objects
        bool isFruit = e->kind >= ENT_FRUIT_ORANGE && e->kind <= ENT_FRUIT_STRAWBERRY;
        float dw = isFruit ? TILE_SIZE : (float)e->cellW * entityScale;
        float dh = isFruit ? TILE_SIZE : (float)e->cellH * entityScale;
        float sx = roundf(e->x - dw * 0.5f - camX);
        float sy = roundf(e->y - dh * 0.5f - camY);
        if (sx < -192.0f || sx > SCREEN_WIDTH + 192.0f ||
            sy < -192.0f || sy > SCREEN_HEIGHT + 192.0f) continue;

        // Rino/Chicken/Plant strips are authored facing left: mirror them so
        // they face the direction they move (or shoot) in
        int dfacing = e->facing;
        if (e->kind == ENT_RINO || e->kind == ENT_CHICKEN || e->kind == ENT_PLANT) dfacing = -dfacing;
        draw_sheet_frame(img, e->cellW, e->cellH, frame, sx, sy, dw, dh, dfacing, 255);
    }
}

// Loads and parses map.json exported from Tiled using cJSON
static void load_map_json(const char* filepath) {
    FILE* file = fopen(filepath, "rb"); //default path is at OutDir/Assets/Terrain/map.json
    if (!file) { // if failed to open map .json, try fallbacks (open in different file path)
        printf("FAILED to open map: %s, trying fallbacks...\n", filepath);
        // Fallbacks for different VS working-dir setups
        const char* fallbacks[] = {
            "../Assets/Terrain/map.json",
            "../../Assets/Terrain/map.json",
            "Terrain/map.json",
            "map.json"
        };
        for (int i = 0; i < 4 && !file; i++) {
            file = fopen(fallbacks[i], "rb");
            if (file) printf("Opened map via fallback: %s\n", fallbacks[i]);
        }
        if (!file) return;
    }

    // Find the length of the file 
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Dynamically allocates memory to store the file's raw text
    char* buffer = (char*)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        return;
    }

    // Reads the file's contents into your allocated buffer
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    // Parses the raw string buffer into a structured cJSON tree object
    cJSON* json = cJSON_Parse(buffer);
    free(buffer);
    if (!json) return;

    cJSON* width = cJSON_GetObjectItemCaseSensitive(json, "width");
    cJSON* height = cJSON_GetObjectItemCaseSensitive(json, "height");
    cJSON* tilew = cJSON_GetObjectItemCaseSensitive(json, "tilewidth");
    cJSON* tileh = cJSON_GetObjectItemCaseSensitive(json, "tileheight");

    if (cJSON_IsNumber(width)) mapWidth = width->valueint;
    if (cJSON_IsNumber(height)) mapHeight = height->valueint;
    if (cJSON_IsNumber(tilew)) tileWidth = tilew->valueint;
    if (cJSON_IsNumber(tileh)) tileHeight = tileh->valueint;
    if (tileWidth > 0) entityScale = TILE_SIZE / (float)tileWidth;

    cJSON* layers = cJSON_GetObjectItemCaseSensitive(json, "layers");
    if (cJSON_IsArray(layers)) {
        process_layers_world(layers);   // tile layer + objectgroups (incl. nested groups)
    }

    cJSON_Delete(json);

    {  // per-kind breakdown so map points can be verified in the console
        int counts[ENT_COUNT] = { 0 };
        for (int i = 0; i < entityCount; i++)
            if (entityPool[i].kind < ENT_COUNT) counts[entityPool[i].kind]++;
        printf("Entities: saw %d, spikes %d, plant %d, chicken %d, bat %d, rino %d, start %d, checkpoint %d, end %d, orange %d, cherries %d, kiwi %d, melon %d, strawberry %d\n",
            counts[ENT_SAW], counts[ENT_SPIKES], counts[ENT_PLANT], counts[ENT_CHICKEN], counts[ENT_BAT], counts[ENT_RINO],
            counts[ENT_START], counts[ENT_CHECKPOINT], counts[ENT_END], counts[ENT_FRUIT_ORANGE], counts[ENT_FRUIT_CHERRIES],
            counts[ENT_FRUIT_KIWI], counts[ENT_FRUIT_MELON], counts[ENT_FRUIT_STRAWBERRY]);
    }
    printf("Map loaded: %dx%d, tile %dx%d, entities %d\n", mapWidth, mapHeight, tileWidth, tileHeight, entityCount);
}

// Renders the tilemap parsed from map.json using Terrain (16x16).png
static void render_map_json(void) {
    if (!mapData || !terrainSheet) return;

    CP_Settings_ImageMode(CP_POSITION_CORNER);
    int tilesPerRowInSheet = (int)(sheetW / tileWidth);
    if (tilesPerRowInSheet < 1) tilesPerRowInSheet = 1;

    for (int y = 0; y < mapHeight; y++) {
        for (int x = 0; x < mapWidth; x++) {
            int tileIndex = y * mapWidth + x;
            int tileGid = mapData[tileIndex];

            if (tileGid <= 0) continue;

            float screenX = x * TILE_SIZE - camX;
            float screenY = y * TILE_SIZE - camY;

            if (screenX < -TILE_SIZE || screenX > SCREEN_WIDTH ||
                screenY < -TILE_SIZE || screenY > SCREEN_HEIGHT) continue;

            int tileId = tileGid - 1;
            float cropX = (float)((tileId % tilesPerRowInSheet) * tileWidth);
            float cropY = (float)((tileId / tilesPerRowInSheet) * tileHeight);

            CP_Image_DrawSubImage(terrainSheet, screenX, screenY, TILE_SIZE, TILE_SIZE, cropX, cropY, cropX + (float)tileWidth, cropY + (float)tileHeight, 255);
        }
    }
}

// Renders the scaling transition over screen when going from menu to gameplay (In & Out)
static void draw_transition(void) {
    if (!isTransitioning || !transitionImage) return;

    transitionTimer += CP_System_GetDt();
    float progress = transitionTimer / TRANSITION_DURATION;

    // Phase 1: Expanding outward to cover the screen
    if (progress <= 0.5f) {
        float expandProgress = progress * 2.0f;
        float maxScale = sqrtf(screenW * screenW + screenH * screenH);
        float currentSize = expandProgress * maxScale;

        CP_Settings_ImageMode(CP_POSITION_CENTER);
        CP_Image_Draw(transitionImage, SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f, currentSize, currentSize, 255);
        CP_Settings_ImageMode(CP_POSITION_CORNER);
    }
    // Phase 2: Switch to GAME state and shrink diamond back to center
    else if (progress < 1.0f) {
        currentState = STATE_GAME;

        float shrinkProgress = 1.0f - ((progress - 0.5f) * 2.0f);
        float maxScale = sqrtf(screenW * screenW + screenH * screenH);
        float currentSize = shrinkProgress * maxScale;

        CP_Settings_ImageMode(CP_POSITION_CENTER);
        CP_Image_Draw(transitionImage, SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f, currentSize, currentSize, 255);
        CP_Settings_ImageMode(CP_POSITION_CORNER);
    }
    // Transition complete
    else {
        isTransitioning = false;
        transitionTimer = 0.0f;
    }
}

// Draws the Title Screen UI
static void draw_cream_panel(float cx, float cy, float w, float h);  // defined with pause menu below
static void draw_menu_ui(void) {
    float centerX = SCREEN_WIDTH * 0.5f;
    float centerY = SCREEN_HEIGHT * 0.5f;

    // Responsive card: ~62% of width, ~70% of height, centered on screen
    float panelW = SCREEN_WIDTH * 0.62f;
    float panelH = SCREEN_HEIGHT * 0.70f;
    float panelTop = centerY - panelH * 0.5f;

    draw_cream_panel(centerX, centerY, panelW, panelH);

    if (gameFont) {
        CP_Font_Set(gameFont);
    }

    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    if (showSettings) {
        // --- SETTINGS OVERLAY ---
        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
        CP_Settings_TextSize(panelH * 0.108f);
        CP_Font_DrawText("CONTROLS & SETTINGS", centerX, panelTop + panelH * 0.22f);

        CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
        CP_Settings_TextSize(panelH * 0.064f);
        CP_Font_DrawText("A / D  -  Move Left / Right", centerX, panelTop + panelH * 0.38f);
        CP_Font_DrawText("SHIFT  -  Hold to Sprint", centerX, panelTop + panelH * 0.48f);
        CP_Font_DrawText("SPACE  -  Jump / Double Jump", centerX, panelTop + panelH * 0.58f);

        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
        CP_Settings_TextSize(panelH * 0.056f);
        CP_Font_DrawText("Click anywhere on card to go BACK", centerX, panelTop + panelH * 0.73f);
    }
    else {
        // --- MAIN MENU OVERLAY ---
        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
        CP_Settings_TextSize(panelH * 0.144f);
        CP_Font_DrawText("PIXEL ADVENTURE", centerX, panelTop + panelH * 0.22f);

        CP_Settings_Fill(CP_Color_Create(80, 80, 80, 255));
        CP_Settings_TextSize(panelH * 0.056f);
        CP_Font_DrawText("Press [ SPACE ] or click PLAY button to Start", centerX, panelTop + panelH * 0.36f);

        float btnSize = panelH * 0.128f;
        float btnY = panelTop + panelH * 0.58f;
        float btnOffset = panelW * 0.125f;

        if (playBtnImage) {
            CP_Image_Draw(playBtnImage, centerX - (btnSize * 0.5f), btnY, btnSize, btnSize, 255);
        }

        if (volBtnImage) {
            float vx = centerX - (btnSize * 0.5f) - btnOffset;
            CP_Image_Draw(volBtnImage, vx, btnY, btnSize, btnSize, muted ? 140 : 255);
            if (muted) {  // red X = muted
                float inset = btnSize * 0.125f;
                CP_Settings_Stroke(CP_Color_Create(180, 30, 25, 255));
                CP_Settings_StrokeWeight(panelH * 0.014f);
                CP_Graphics_DrawLine(vx + inset, btnY + inset, vx + btnSize - inset, btnY + btnSize - inset);
                CP_Graphics_DrawLine(vx + btnSize - inset, btnY + inset, vx + inset, btnY + btnSize - inset);
                CP_Settings_NoStroke();
            }
        }

        if (settingsBtnImage) {
            CP_Image_Draw(settingsBtnImage, centerX - (btnSize * 0.5f) + btnOffset, btnY, btnSize, btnSize, 255);
        }
    }
}

// Handles input during the menu screen
static void handle_menu_input(void) {
    if (isTransitioning) return;

    if (CP_Input_KeyTriggered(KEY_SPACE)) {
        isTransitioning = true;
        transitionTimer = 0.0f;
    }

    if (CP_Input_MouseClicked()) {
        float centerX = SCREEN_WIDTH * 0.5f;
        float panelW = SCREEN_WIDTH * 0.62f;
        float panelH = SCREEN_HEIGHT * 0.70f;
        float panelTop = SCREEN_HEIGHT * 0.5f - panelH * 0.5f;

        if (showSettings) {
            if (is_mouse_over(centerX - (panelW * 0.5f), panelTop, panelW, panelH)) {
                showSettings = false;
                play_click();
            }
        }
        else {
            float btnSize = panelH * 0.128f;
            float btnY = panelTop + panelH * 0.58f;
            float btnOffset = panelW * 0.125f;

            float playX = centerX - (btnSize * 0.5f);
            float volX = playX - btnOffset;
            float settingsX = playX + btnOffset;

            if (is_mouse_over(playX, btnY, btnSize, btnSize)) {
                play_click();
                isTransitioning = true;
                transitionTimer = 0.0f;
            }

            // Volume button: toggles mute (red X shows muted)
            if (is_mouse_over(volX, btnY, btnSize, btnSize)) {
                muted = !muted;
                apply_volumes();
                play_click();
            }

            if (is_mouse_over(settingsX, btnY, btnSize, btnSize)) {
                showSettings = true;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                play_click();
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Lifecycle Functions
// -----------------------------------------------------------------------------

void game_init(void) {
    CP_System_SetWindowSize((int)screenW, (int)screenH);
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // Lock the loop to the display refresh rate to avoid screen tearing (no vsync API in CProcessing)
    int refresh = CP_System_GetDisplayRefreshRate();
    if (refresh > 0) CP_System_SetFrameRate((float)refresh);

    // Reference canvas = the intended window size; all layout scales off this
    if (refW <= 0.0f) { refW = screenW; refH = screenH; }

    CP_System_SetWindowTitle("Pixel Adventure");

    CP_Settings_ImageMode(CP_POSITION_CORNER);
    CP_Settings_ImageFilterMode(CP_IMAGE_FILTER_NEAREST);

    bgImage = CP_Image_Load("Assets/Background/Brown.png");
    gameFont = CP_Font_Load("Assets/ThaleahFat.ttf");

    playBtnImage = CP_Image_Load("Assets/Menu/Buttons/Play.png");
    volBtnImage = CP_Image_Load("Assets/Menu/Buttons/Volume.png");
    settingsBtnImage = CP_Image_Load("Assets/Menu/Buttons/Settings.png");
    restartBtnImage = CP_Image_Load("Assets/Menu/Buttons/Restart.png");

    transitionImage = CP_Image_Load("Assets/Other/Transition.png");

    terrainSheet = CP_Image_Load("Assets/Terrain/Terrain (16x16).png");
    if (terrainSheet) {
        sheetW = (float)CP_Image_GetWidth(terrainSheet);
        sheetH = (float)CP_Image_GetHeight(terrainSheet);
    }

    load_map_json("Assets/Terrain/map.json");

    // New run starts at the first "Start" checkpoint placed in the map
    for (int i = 0; i < entityCount; i++) {
        if (entityPool[i].kind == ENT_START) {
            respawnX = entityPool[i].x;
            respawnY = entityPool[i].y;
            break;
        }
    }

    // Virtual Guy sprites (32x32 cells, horizontal strips)
    playerIdle = CP_Image_Load("Assets/Main Characters/Virtual Guy/Idle (32x32).png");
    playerRun = CP_Image_Load("Assets/Main Characters/Virtual Guy/Run (32x32).png");
    playerJump = CP_Image_Load("Assets/Main Characters/Virtual Guy/Jump (32x32).png");
    playerDouble = CP_Image_Load("Assets/Main Characters/Virtual Guy/Double Jump (32x32).png");
    playerFall = CP_Image_Load("Assets/Main Characters/Virtual Guy/Fall (32x32).png");
    shadowImg = CP_Image_Load("Assets/Other/Shadow.png");
    dustImg = CP_Image_Load("Assets/Other/Dust Particle.png");
    heartImg = CP_Image_Load("Assets/Main Characters/Hearts.png");

    // Audio: streamed music + one-shot SFX
    menuMusic = CP_Sound_LoadMusic("Assets/Sound/menu_music.wav");
    worldMusic = CP_Sound_LoadMusic("Assets/Sound/world_music.wav");
    stepSnd = CP_Sound_Load("Assets/Sound/step.wav");
    jumpSnd = CP_Sound_Load("Assets/Sound/jump.wav");
    respawnSnd = CP_Sound_Load("Assets/Sound/respawn.wav");
    clickSnd = CP_Sound_Load("Assets/Sound/button_click.wav");
    playerHitSnd = CP_Sound_Load("Assets/Sound/player_hit.wav");
    hitSnd = CP_Sound_Load("Assets/Sound/hit.wav");
    deathSnd = CP_Sound_Load("Assets/Sound/death.wav");
    coinSnd = CP_Sound_Load("Assets/Sound/coin.wav");
    windowOpenSnd = CP_Sound_Load("Assets/Sound/window_open.ogg");
    plantAtkImg = CP_Image_Load("Assets/Enemies/Plant/Attack (44x42).png");
    bulletImg = CP_Image_Load("Assets/Enemies/Plant/Bullet.png");
    piecesImg = CP_Image_Load("Assets/Enemies/Plant/Bullet Pieces.png");
    apply_volumes();

    reset_run();
    if (menuMusic) {
        CP_Sound_PlayAdvanced(menuMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
        musicMode = 1;
    }
}

// Cream panel helper (original menu card style: shadow + cream + gold border)
static void draw_cream_panel(float cx, float cy, float w, float h) {
    CP_Settings_RectMode(CP_POSITION_CENTER);
    CP_Settings_Fill(CP_Color_Create(40, 20, 50, 220));
    CP_Graphics_DrawRect(cx, cy, w + SX(16.0f), h + SY(16.0f));
    CP_Settings_Fill(CP_Color_Create(245, 230, 210, 240));
    CP_Graphics_DrawRect(cx, cy, w, h);
    CP_Settings_NoFill();
    CP_Settings_Stroke(CP_Color_Create(215, 140, 40, 255));
    CP_Settings_StrokeWeight(SF(6.0f));
    CP_Graphics_DrawRect(cx, cy, w - SX(24.0f), h - SY(24.0f));
    CP_Settings_NoStroke();
    CP_Settings_RectMode(CP_POSITION_CORNER);
}

// Pause menu rows (page 0: main, page 1: audio). Row i center-y = 310 + i * 70 (design space).
static float pause_row_y(int i) { return SY(310.0f + (float)i * 70.0f); }

static void draw_pause_menu(void) {
    // Dark overlay over the frozen world (PixelAdventure uses 0,0,0,128)
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    // Banner
    draw_cream_panel(SCREEN_WIDTH * 0.5f, SY(140.0f), SX(420.0f), SY(96.0f));
    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(SF(54.0f));
    CP_Font_DrawText(pausePage == 0 ? "PAUSED" : "OPTIONS", SCREEN_WIDTH * 0.5f, SY(140.0f));

    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();

    // Top panel
    draw_cream_panel(SCREEN_WIDTH * 0.5f, SY(380.0f), SX(440.0f), SY(300.0f));
    CP_Settings_TextSize(SF(40.0f));
    if (pausePage == 0) {
        const char* rows[] = { "RESUME", "RESTART", "OPTIONS" };
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            bool hover = mx >= SX(450.0f) && mx <= SX(830.0f) && my >= ry - SY(28.0f) && my <= ry + SY(28.0f);
            if (hover) {
                CP_Settings_RectMode(CP_POSITION_CORNER);
                CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
                CP_Graphics_DrawRect(SX(450.0f), ry - SY(28.0f), SX(380.0f), SY(56.0f));
            }
            CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
            CP_Font_DrawText(rows[i], SCREEN_WIDTH * 0.5f, ry);
        }
    }
    else {
        // Audio rows: label + [-] value [+]
        const char* rows[] = { "MUSIC", "SFX", "BACK" };
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            if (i < 2) {
                float vol = (i == 0) ? musicVol : sfxVol;
                bool hovMinus = mx >= SX(670.0f) && mx <= SX(718.0f) && my >= ry - SY(24.0f) && my <= ry + SY(24.0f);
                bool hovPlus = mx >= SX(806.0f) && mx <= SX(854.0f) && my >= ry - SY(24.0f) && my <= ry + SY(24.0f);
                CP_Settings_RectMode(CP_POSITION_CORNER);
                CP_Settings_Fill(hovMinus ? CP_Color_Create(215, 140, 40, 160) : CP_Color_Create(40, 40, 40, 40));
                CP_Graphics_DrawRect(SX(670.0f), ry - SY(24.0f), SX(48.0f), SY(48.0f));
                CP_Settings_Fill(hovPlus ? CP_Color_Create(215, 140, 40, 160) : CP_Color_Create(40, 40, 40, 40));
                CP_Graphics_DrawRect(SX(806.0f), ry - SY(24.0f), SX(48.0f), SY(48.0f));
                CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
                CP_Font_DrawText("-", SX(694.0f), ry);
                CP_Font_DrawText("+", SX(830.0f), ry);
                char vbuf[16];
                sprintf(vbuf, "%d%%", (int)(vol * 100.0f + 0.5f));
                CP_Font_DrawText(vbuf, SX(762.0f), ry);
                CP_Font_DrawText(rows[i], SX(555.0f), ry);
            }
            else {
                bool hover = mx >= SX(450.0f) && mx <= SX(830.0f) && my >= ry - SY(28.0f) && my <= ry + SY(28.0f);
                if (hover) {
                    CP_Settings_RectMode(CP_POSITION_CORNER);
                    CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
                    CP_Graphics_DrawRect(SX(450.0f), ry - SY(28.0f), SX(380.0f), SY(56.0f));
                }
                CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
                CP_Font_DrawText(rows[i], SCREEN_WIDTH * 0.5f, ry);
            }
        }
    }

    // Bottom panel: MENU | QUIT (PixelAdventure has Menu + Desktop)
    draw_cream_panel(SCREEN_WIDTH * 0.5f, SY(600.0f), SX(440.0f), SY(100.0f));
    bool hovMenu = mx >= SX(430.0f) && mx <= SX(630.0f) && my >= SY(572.0f) && my <= SY(628.0f);
    bool hovQuit = mx >= SX(650.0f) && mx <= SX(850.0f) && my >= SY(572.0f) && my <= SY(628.0f);
    CP_Settings_RectMode(CP_POSITION_CORNER);
    if (hovMenu) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(SX(430.0f), SY(572.0f), SX(200.0f), SY(56.0f));
    }
    if (hovQuit) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(SX(650.0f), SY(572.0f), SX(200.0f), SY(56.0f));
    }
    CP_Settings_Fill(hovMenu ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("MENU", SX(530.0f), SY(600.0f));
    CP_Settings_Fill(hovQuit ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("QUIT", SX(750.0f), SY(600.0f));
}

static void handle_pause_input(void) {
    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();
    bool clicked = CP_Input_MouseClicked();
    if (pausePage == 0) {
        if (!clicked) return;
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            if (mx >= SX(450.0f) && mx <= SX(830.0f) && my >= ry - SY(28.0f) && my <= ry + SY(28.0f)) {
                if (i == 0) paused = false;                    // RESUME
                else if (i == 1) { reset_run(); paused = false; }  // RESTART
                else { pausePage = 1; if (windowOpenSnd) CP_Sound_Play(windowOpenSnd); }  // OPTIONS
                play_click();
                return;
            }
        }
        if (mx >= SX(430.0f) && mx <= SX(630.0f) && my >= SY(572.0f) && my <= SY(628.0f)) {
            currentState = STATE_MENU;  // MENU: back to title
            paused = false;
            runStarted = false;
            if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            play_click();
        }
        else if (mx >= SX(650.0f) && mx <= SX(850.0f) && my >= SY(572.0f) && my <= SY(628.0f)) {
            CP_Engine_Terminate();      // QUIT: close the game
        }
    }
    else {
        if (!clicked) return;
        for (int i = 0; i < 2; i++) {
            float ry = pause_row_y(i);
            float* vol = (i == 0) ? &musicVol : &sfxVol;
            if (mx >= SX(670.0f) && mx <= SX(718.0f) && my >= ry - SY(24.0f) && my <= ry + SY(24.0f)) {
                *vol -= 0.1f; if (*vol < 0.0f) *vol = 0.0f;
                apply_volumes();
                play_click();
                return;
            }
            if (mx >= SX(806.0f) && mx <= SX(854.0f) && my >= ry - SY(24.0f) && my <= ry + SY(24.0f)) {
                *vol += 0.1f; if (*vol > 1.0f) *vol = 1.0f;
                apply_volumes();
                if (i == 1 && stepSnd) CP_Sound_Play(stepSnd);
                else play_click();
                return;
            }
        }
        float ry = pause_row_y(2);
        if (mx >= SX(450.0f) && mx <= SX(830.0f) && my >= ry - SY(28.0f) && my <= ry + SY(28.0f)) { pausePage = 0; play_click(); }  // BACK
        if (mx >= SX(430.0f) && mx <= SX(630.0f) && my >= SY(572.0f) && my <= SY(628.0f)) {
            currentState = STATE_MENU;
            paused = false;
            runStarted = false;
            if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            play_click();
        }
        else if (mx >= SX(650.0f) && mx <= SX(850.0f) && my >= SY(572.0f) && my <= SY(628.0f)) {
            CP_Engine_Terminate();
        }
    }
}

void game_update(void) {
    // Keep the logical screen in sync with the fullscreen (or resized) window
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // State Logic
    if (currentState == STATE_MENU) {
        // Clear screen and draw menu background
        CP_Graphics_ClearBackground(CP_Color_Create(0, 0, 0, 255));
        bgScroll += BG_SCROLL_SPEED * CP_System_GetDt();
        if (musicMode != 1 && menuMusic) {
            CP_Sound_StopGroup(CP_SOUND_GROUP_MUSIC);
            CP_Sound_PlayAdvanced(menuMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
            musicMode = 1;
        }
        draw_background();

        draw_menu_ui();
        handle_menu_input();
        draw_transition();
    }
    else if (currentState == STATE_GAME) {
        float dt = CP_System_GetDt();
        if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;  // clamp hitches, prevents tunneling

        // ESC ("exit" key): open pause menu, step back a page, or resume
        if (CP_Input_KeyTriggered(KEY_ESCAPE)) {
            if (!paused) {
                paused = true; pausePage = 0;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            }
            else if (pausePage == 1) pausePage = 0;
            else paused = false;
        }

        if (!paused) {
            if (!runStarted) { reset_run(); runStarted = true; }
            bgScroll += BG_SCROLL_SPEED * dt;
            if (musicMode != 2 && worldMusic) {
                CP_Sound_StopGroup(CP_SOUND_GROUP_MUSIC);
                CP_Sound_PlayAdvanced(worldMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
                musicMode = 2;
            }

            if (!levelComplete) {
            // --- Input: A/D move, SHIFT sprint, SPACE jump/double ---
            bool left = CP_Input_KeyDown(KEY_A);
            bool right = CP_Input_KeyDown(KEY_D);
            bool sprint = CP_Input_KeyDown(KEY_LEFT_SHIFT) || CP_Input_KeyDown(KEY_RIGHT_SHIFT);
            float runSpeed = sprint ? SPRINT_SPEED : MOVE_SPEED;
            if (player.lockT > 0.0f) player.vx = player.kbx;
            else if (left && !right) { player.vx = -runSpeed; player.facing = -1; }
            else if (right && !left) { player.vx = runSpeed; player.facing = 1; }
            else player.vx = 0.0f;

            if (CP_Input_KeyTriggered(KEY_SPACE)) player.buffer = JUMP_BUFFER;

            // --- Jump / double jump ---
            if (player.buffer > 0.0f) {
                if (player.grounded || player.coyote > 0.0f) {
                    player.vy = JUMP_VEL;
                    player.jumps = 1;
                    player.grounded = false;
                    player.coyote = 0.0f; player.buffer = 0.0f;
                    if (jumpSnd) CP_Sound_PlayAdvanced(jumpSnd, 0.9f, 1.0f, (CP_BOOL)0, CP_SOUND_GROUP_SFX);
                    spawn_dust(player.x + PLAYER_HB_W * 0.5f, player.y + PLAYER_HB_H - 4.0f, 0.0f, -40.0f, 0.35f, 22.0f);
                    spawn_dust(player.x + PLAYER_HB_W * 0.5f - 14.0f * (float)player.facing, player.y + PLAYER_HB_H - 4.0f, -120.0f * (float)player.facing, -40.0f, 0.35f, 18.0f);
                }
                else if (player.jumps < 2) {
                    player.vy = DOUBLE_JUMP_VEL;
                    player.jumps = 2;
                    player.buffer = 0.0f;
                    player.doubleAnim = true; player.doubleAnimT = 0.0f;
                    if (jumpSnd) CP_Sound_PlayAdvanced(jumpSnd, 0.9f, 1.25f, (CP_BOOL)0, CP_SOUND_GROUP_SFX);
                    for (int k = 0; k < 6; k++)
                        spawn_dust(player.x + PLAYER_HB_W * 0.5f + (float)((k % 3) - 1) * 16.0f, player.y + PLAYER_HB_H - 10.0f - (float)(k / 3) * 18.0f, 0.0f, 30.0f, 0.4f, 20.0f);
                }
            }
            if (CP_Input_KeyReleased(KEY_SPACE) && player.vy < -350.0f) player.vy = -350.0f;  // variable height

            // --- Gravity ---
            player.vy += GRAVITY * dt;
            if (player.vy > MAX_FALL) player.vy = MAX_FALL;

            if (player.grounded) player.coyote = COYOTE_TIME; else player.coyote -= dt;
            player.buffer -= dt;
            player.animT += dt;
            if (player.lockT > 0.0f) { player.lockT -= dt; player.kbx -= player.kbx * 8.0f * dt; }
            if (player.invulnT > 0.0f) player.invulnT -= dt;
            if (player.doubleAnim) player.doubleAnimT += dt;

            player_physics(dt);

            // --- Footsteps + run dust (quicker steps while sprinting) ---
            if (player.grounded && (player.vx > 20.0f || player.vx < -20.0f)) {
                player.stepT += dt;
                if (player.stepT >= (sprint ? 0.22f : 0.30f)) {
                    player.stepT = 0.0f;
                    if (stepSnd) CP_Sound_Play(stepSnd);
                    spawn_dust(player.x + PLAYER_HB_W * 0.5f - 16.0f * (float)player.facing, player.y + PLAYER_HB_H - 4.0f, -60.0f * (float)player.facing, -50.0f, 0.4f, 18.0f);
                }
            }
            else player.stepT = 0.0f;

            // --- Dust sim ---
            for (int i = 0; i < MAX_DUST; i++) {
                if (!dustPool[i].active) continue;
                dustPool[i].life -= dt;
                if (dustPool[i].life <= 0.0f) { dustPool[i].active = false; continue; }
                dustPool[i].x += dustPool[i].vx * dt;
                dustPool[i].y += dustPool[i].vy * dt;
                dustPool[i].vy -= 300.0f * dt;
            }

            // --- Camera follows player, clamped + pixel-snapped ---
            camX = player.x + PLAYER_HB_W * 0.5f - SCREEN_WIDTH * 0.5f;
            camY = player.y + PLAYER_HB_H * 0.5f - SCREEN_HEIGHT * 0.5f;
            float worldW = (float)(mapWidth * (int)TILE_SIZE);
            float worldH = (float)(mapHeight * (int)TILE_SIZE);
            if (camX < 0.0f) camX = 0.0f;
            if (camY < 0.0f) camY = 0.0f;
            if (camX > worldW - SCREEN_WIDTH) camX = worldW - SCREEN_WIDTH;
            if (camY > worldH - SCREEN_HEIGHT) camY = worldH - SCREEN_HEIGHT;
            camX = roundf(camX);
            camY = roundf(camY);

            // Fail-safe: fell out of the world somehow -> respawn, never stranded
            if (player.y > worldH + 200.0f || player.x < -600.0f || player.x > worldW + 600.0f) {
                reset_run();
                camX = player.x + PLAYER_HB_W * 0.5f - SCREEN_WIDTH * 0.5f;
                camY = player.y + PLAYER_HB_H * 0.5f - SCREEN_HEIGHT * 0.5f;
                if (camX < 0.0f) camX = 0.0f;
                if (camY < 0.0f) camY = 0.0f;
            }

            update_entities(dt);
            check_player_entity_collisions();
            update_bullets(dt);
            }
        } // end of world update (skipped while paused)

        // ================= RENDER =================
        CP_Graphics_ClearBackground(CP_Color_Create(33, 31, 48, 255));
        draw_background();      // brown shows through empty (gid 0) cells
        render_map_json();      // navy tiles cover the rest
        render_entities();      // checkpoints, enemies, traps from map.json
        draw_bullets();           // plant seeds + impact pieces
        CP_Settings_ImageMode(CP_POSITION_CORNER);

        // Shadow: cast down from feet, shrink/fade with distance
        {
            float feetCX = player.x + PLAYER_HB_W * 0.5f;
            float feetY = player.y + PLAYER_HB_H;
            float groundTop = -1.0f;
            for (float wy = feetY + 4.0f; wy < feetY + 300.0f; wy += 6.0f) {
                if (is_solid_px(feetCX, wy)) {
                    groundTop = ((int)(wy / TILE_SIZE)) * TILE_SIZE;
                    break;
                }
            }
            if (shadowImg && groundTop > 0.0f) {
                float dist = groundTop - feetY;
                float k = 1.0f - dist / 300.0f;
                if (k < 0.0f) k = 0.0f;
                float sw = 20.0f + 28.0f * k;
                int alpha = 60 + (int)(140.0f * k);
                float u = (float)CP_Image_GetWidth(shadowImg);
                float v = (float)CP_Image_GetHeight(shadowImg);
                CP_Image_DrawSubImage(shadowImg, roundf(feetCX - sw * 0.5f - camX), roundf(groundTop - 10.0f - camY), sw, 14.0f, 0.0f, 0.0f, u, v, alpha);
            }
        }

        // Player animation select
        {
            CP_Image img = playerIdle;
            int frame = 0;
            if (!player.grounded) {
                if (player.vy < 0.0f) {
                    if (player.doubleAnim || player.jumps >= 2) {
                        img = playerDouble;
                        frame = (int)(player.doubleAnimT * 12.0f);
                        if (frame > 5) frame = 5;
                    }
                    else { img = playerJump; frame = 0; }
                }
                else { img = playerFall; frame = 0; }
            }
            else if (player.vx > 20.0f || player.vx < -20.0f) {
                img = playerRun;
                frame = ((int)(player.animT * 12.0f)) % 12;
            }
            else {
                img = playerIdle;
                frame = ((int)(player.animT * 8.0f)) % 11;
            }
            int pAlpha = (player.invulnT > 0.0f && ((int)(player.invulnT * 14.0f) % 2 == 0)) ? 130 : 255;
            draw_sprite_frame(img, frame, roundf(player.x - PLAYER_HB_OX - camX), roundf(player.y - PLAYER_HB_OY - camY), 32.0f * PLAYER_SCALE, player.facing, pAlpha);
        }

        // Dust (fade out)
        for (int i = 0; i < MAX_DUST; i++) {
            if (!dustPool[i].active || !dustImg) continue;
            float k = dustPool[i].life / dustPool[i].maxLife;
            int alpha = (int)(220.0f * k);
            float s = dustPool[i].size * (1.2f - 0.4f * k);
            float u = (float)CP_Image_GetWidth(dustImg);
            float v = (float)CP_Image_GetHeight(dustImg);
            CP_Image_DrawSubImage(dustImg, roundf(dustPool[i].x - s * 0.5f - camX), roundf(dustPool[i].y - s * 0.5f - camY), s, s, 0.0f, 0.0f, u, v, alpha);
        }

        // HUD: hearts row (top-left)
        if (heartImg) {
            for (int i = 0; i < MAX_HEARTS; i++)
                draw_sheet_frame(heartImg, 16, 16, (i < hearts) ? 0 : 1, SX(16.0f) + (float)i * SX(48.0f), SY(14.0f), SX(48.0f), SY(48.0f), 1, 255);
        }

        // Level complete overlay (End object reached)
        if (levelComplete) {
            draw_cream_panel(SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f, SX(560.0f), SY(280.0f));
            if (gameFont) CP_Font_Set(gameFont);
            CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
            CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
            CP_Settings_TextSize(SF(58.0f));
            CP_Font_DrawText("LEVEL COMPLETE", SCREEN_WIDTH * 0.5f, SY(310.0f));
            CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
            CP_Settings_TextSize(SF(30.0f));
            CP_Font_DrawText("Click anywhere to return to menu", SCREEN_WIDTH * 0.5f, SY(400.0f));
            if (CP_Input_MouseClicked()) {
                currentState = STATE_MENU;
                runStarted = false;
                paused = false;
                levelComplete = false;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            }
        }

        if (!paused) {
            // Restart button (top-right): click = respawn + reset score
            if (restartBtnImage) {
                float rx = SCREEN_WIDTH - SX(64.0f);
                float ry = SY(16.0f);
                float rsize = SX(48.0f);
                bool hover = is_mouse_over(rx, ry, rsize, rsize);
                float rs = hover ? rsize + SX(6.0f) : rsize;
                float ro = (rs - rsize) * 0.5f;
                CP_Image_Draw(restartBtnImage, rx - ro, ry - ro, rs, rs, 255);
                if (hover && CP_Input_MouseClicked()) { play_click(); reset_run(); }
            }

            // Options button (top-right, left of restart): opens the pause menu
            if (settingsBtnImage) {
                float sx = SCREEN_WIDTH - SX(64.0f) - SX(56.0f);
                float sy = SY(16.0f);
                float ssize = SX(48.0f);
                bool hover = is_mouse_over(sx, sy, ssize, ssize);
                float ss = hover ? ssize + SX(6.0f) : ssize;
                float so = (ss - ssize) * 0.5f;
                CP_Image_Draw(settingsBtnImage, sx - so, sy - so, ss, ss, 255);
                if (hover && CP_Input_MouseClicked()) {
                    paused = true; pausePage = 0;
                    if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                    play_click();
                }
            }

            draw_transition();
        }
        else {
            draw_pause_menu();
            handle_pause_input();
        }
    }
}

void game_exit(void) {
    // Free allocated memory on shutdown
    if (bgImage) CP_Image_Free(bgImage);
    if (gameFont) CP_Font_Free(gameFont);

    if (playBtnImage) CP_Image_Free(playBtnImage);
    if (volBtnImage) CP_Image_Free(volBtnImage);
    if (settingsBtnImage) CP_Image_Free(settingsBtnImage);
    if (restartBtnImage) CP_Image_Free(restartBtnImage);
    if (transitionImage) CP_Image_Free(transitionImage);
    if (terrainSheet) CP_Image_Free(terrainSheet);
    if (playerIdle) CP_Image_Free(playerIdle);
    if (playerRun) CP_Image_Free(playerRun);
    if (playerJump) CP_Image_Free(playerJump);
    if (playerDouble) CP_Image_Free(playerDouble);
    if (playerFall) CP_Image_Free(playerFall);
    if (shadowImg) CP_Image_Free(shadowImg);
    if (dustImg) CP_Image_Free(dustImg);
    if (heartImg) CP_Image_Free(heartImg);
    for (int i = 0; i < entityCount; i++) {
        if (entityPool[i].baseImage) CP_Image_Free(entityPool[i].baseImage);
        if (entityPool[i].altImage) CP_Image_Free(entityPool[i].altImage);
    }
    if (plantAtkImg) CP_Image_Free(plantAtkImg);
    if (bulletImg) CP_Image_Free(bulletImg);
    if (piecesImg) CP_Image_Free(piecesImg);
    CP_Sound_StopAll();
    if (playerHitSnd) CP_Sound_Free(playerHitSnd);
    if (hitSnd) CP_Sound_Free(hitSnd);
    if (deathSnd) CP_Sound_Free(deathSnd);
    if (coinSnd) CP_Sound_Free(coinSnd);
    if (menuMusic) CP_Sound_Free(menuMusic);
    if (worldMusic) CP_Sound_Free(worldMusic);
    if (stepSnd) CP_Sound_Free(stepSnd);
    if (jumpSnd) CP_Sound_Free(jumpSnd);
    if (respawnSnd) CP_Sound_Free(respawnSnd);
    if (clickSnd) CP_Sound_Free(clickSnd);
    if (windowOpenSnd) CP_Sound_Free(windowOpenSnd);
    if (mapData) free(mapData);
}

int main(void) {
    CP_Engine_SetNextGameState(game_init, game_update, game_exit);
    CP_Engine_Run(0);
    return 0;
}