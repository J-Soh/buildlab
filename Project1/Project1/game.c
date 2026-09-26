#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.c"
#include "cprocessing.h"
#include "utils.h"  // shared canvas, assets, UI helpers (screen vars, font, sounds, cream panel)
#include "menu.h"   // menu_init/menu_update/menu_exit (pause MENU + level-complete return here)
#include "game.h"   // own state entry points

// Gameplay HUD button (menu buttons moved to menu.c;
// shared canvas/assets/helpers moved to utils.c)
static CP_Image restartBtnImage = NULL;  // gameplay HUD, top-right

// Corner buttons use a fixed 1600x900 top-right layout (48px, no scaling).

// Pause menu (PixelAdventure style: dim + banner + button panels, world frozen)
static bool paused = false;
static int pausePage = 0;  // 0 = main (Resume/Restart/Settings), 1 = audio options

// Terrain Tilesheet Handle & Dimensions (fallback if map.png is missing)
static CP_Image terrainSheet = NULL;
static float sheetW = 384.0f; // Width of Terrain (16x16).png
static float sheetH = 192.0f; // Height of Terrain (16x16).png

// Pre-rendered map (Tiled -> Export as Image at 1x = 3200x1600).
// Drawn as one camera sub-rect per frame; map.json tile data stays for collision only.
static CP_Image mapImage = NULL;

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
#define MOVE_SPEED 550.0f
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

static CP_Image playerIdle = NULL;    // 11 frames
static CP_Image playerRun = NULL;     // 12 frames
static CP_Image playerJump = NULL;    // 1 frame
static CP_Image playerDouble = NULL;  // 6 frames
static CP_Image playerFall = NULL;    // 1 frame
static CP_Image shadowImg = NULL;
static CP_Image dustImg = NULL;

static CP_Sound worldMusic = NULL;
static CP_Sound stepSnd = NULL;
static CP_Sound jumpSnd = NULL;
static CP_Sound respawnSnd = NULL;    // every player spawn


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
    ENT_SAW, ENT_SPIKES, // 0-1
    ENT_PLANT, ENT_CHICKEN, ENT_BAT, ENT_RINO, // 2-5
    ENT_START, ENT_CHECKPOINT, ENT_END, // 6-8
    ENT_FRUIT_ORANGE, ENT_FRUIT_CHERRIES, ENT_FRUIT_KIWI, ENT_FRUIT_MELON, ENT_FRUIT_STRAWBERRY, // 9-13
    ENT_COUNT // 14
} EntityKind;

typedef struct {
    const char* path;
    const char* altPath;   // flattened / activated variant sheet (checkpoint flag)
    const char* idlePath;  // optional Idle strip; NULL = reuse path (Chicken/Bat/Rino)
    const char* runPath;   // optional Run (or Fly) strip; NULL = reuse path
    const char* hitPath;   // optional one-shot Hit strip; NULL = instant remove on stomp
    int cellW, cellH;      // frame cell size on the sheet (source px)
    float hbW, hbH;        // hitbox as a fraction of the scaled cell
    bool hazard;           // non-living damager (saw, spikes)
    bool flies;            // bat: no gravity, hovers
    float speed;           // patrol speed (world px/s); 0 = stationary
    float animFps;         // animation playback speed
} EntityDef;

static const EntityDef ENTITY_DEFS[ENT_COUNT] = {
    { "Assets/Traps/Saw/On (38x38).png",        NULL, NULL, NULL, NULL, 38, 38, 0.55f, 0.55f, true,  false,   0.0f, 12.0f },
    { "Assets/Traps/Spikes/Idle.png",           NULL, NULL, NULL, NULL, 16, 16, 0.70f, 0.45f, true,  false,   0.0f,  1.0f },
    { "Assets/Enemies/Plant/Idle (44x42).png",  NULL, NULL, NULL, "Assets/Enemies/Plant/Hit (44x42).png",
                                                                            44, 42, 0.60f, 0.60f, false, false,   0.0f,  8.0f },
    { "Assets/Enemies/Chicken/Run (32x34).png", NULL, NULL, NULL, "Assets/Enemies/Chicken/Hit (32x34).png",
                                                                            32, 34, 0.68f, 0.68f, false, false, 110.0f, 10.0f },
    { "Assets/Enemies/Bat/Flying (46x30).png",  NULL, "Assets/Enemies/Bat/Idle (46x30).png",
                                                      "Assets/Enemies/Bat/Flying (46x30).png",
                                                      "Assets/Enemies/Bat/Hit (46x30).png",
                                                                            46, 30, 0.60f, 0.60f, false, true,   30.0f,  8.0f },
    { "Assets/Enemies/Rino/Run (52x34).png",    NULL, NULL, NULL, "Assets/Enemies/Rino/Hit (52x34).png",
                                                                            52, 34, 0.68f, 0.68f, false, false, 130.0f,  8.0f },
    { "Assets/Items/Checkpoints/Start/Start (Idle).png", NULL, NULL, NULL, NULL,
                                                                            64, 64, 0.55f, 0.55f, false, false,   0.0f,  1.0f },
    { "Assets/Items/Checkpoints/Checkpoint/Checkpoint (No Flag).png",
      "Assets/Items/Checkpoints/Checkpoint/Checkpoint (Flag Idle)(64x64).png",
      NULL, NULL, NULL,
      64, 64, 0.55f, 0.55f, false, false, 0.0f, 1.0f },
    { "Assets/Items/Checkpoints/End/End (Idle).png", NULL, NULL, NULL, NULL,
                                                                            64, 64, 0.55f, 0.55f, false, false,   0.0f,  1.0f },
    { "Assets/Items/Fruits/Orange.png",     NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, false,   0.0f, 12.0f },
    { "Assets/Items/Fruits/Cherries.png",   NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, false,   0.0f, 12.0f },
    { "Assets/Items/Fruits/Kiwi.png",       NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, false,   0.0f, 12.0f },
    { "Assets/Items/Fruits/Melon.png",      NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, false,   0.0f, 12.0f },
    { "Assets/Items/Fruits/Strawberry.png", NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, false,   0.0f, 12.0f },
};

typedef struct {
    EntityKind kind;
    bool active;
    CP_Image baseImage, altImage;
    CP_Image idleImg, runImg, hitImg; // per-state strips (NULL = reuse baseImage)
    CP_Image emergeImg;              // bat only: Ceiling In alert strip on detection
    int animState;   // 0 = idle/perched, 1 = run/fly, 2 = hit flash, 3 = bat emerging
    float stateT;    // time in current animState
    float deathT;    // >0 = stomped, Hit strip playing, counts down to deactivate
    int totalFrames, cellW, cellH;
    float x, y;            // world px, CENTER of the entity
    float sx, sy;          // spawn point (restored on reset)
    float w, h;            // world-px hitbox, centered on (x, y)
    float vx, vy;
    int facing;
    float animT;
    float atkT, coolT;     // plant shooter: attack clock + cooldown
    bool attacking, fired; // attack anim running / bullet released this cycle
    bool grounded;
    bool activated;        // checkpoint reached -> flag out
} Entity;

static Entity entityPool[MAX_ENTITIES]; // array of 72 slots (defined in line 158)
static int entityCount = 0;        // How many used so far (start at 0).
static float entityScale = 3.0f;   // world px per Tiled px == TILE_SIZE / tileWidth

static bool levelComplete = false;
static float respawnX = -1.0f, respawnY = -1.0f;  // player CENTER, world px (checkpoint; cleared on fresh start)
static float startX = -1.0f, startY = -1.0f;      // player CENTER, world px (level start from ENT_START; never cleared)

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

// Fruit pickup sparkle: Collected.png (32x32 cells, 6-frame one-shot, like
// PixelAdventure's CollectedAnimation). Spawned where a fruit is collected,
// plays once, then frees its slot. Same pool pattern as BulletFX above.
static CP_Image collectedImg = NULL;
#define MAX_PICKUP_FX 16
#define PICKUP_FRAMES 6
#define PICKUP_FPS 18.0f
typedef struct { bool active; float x, y, t; } PickupFX;
static PickupFX pickupFx[MAX_PICKUP_FX];
static void spawn_pickup_fx(float wx, float wy) {
    for (int i = 0; i < MAX_PICKUP_FX; i++) {
        if (!pickupFx[i].active) {
            pickupFx[i].active = true;
            pickupFx[i].x = wx; pickupFx[i].y = wy; pickupFx[i].t = 0.0f;
            return;
        }
    }
}


// -----------------------------------------------------------------------------
// Helper Functions ("Methods")
// -----------------------------------------------------------------------------

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

// Fresh start from the beginning: forget checkpoints, then reset.
// (reset_run alone respawns at the last checkpoint — that stays for death and
// fell-out-of-world rescues. RESTART buttons and menu PLAY use this instead,
// so flags, fruits, enemies AND spawn all start over.)
static void reset_run(void);  // defined just below; fwd so the helper can call it
static void restart_from_beginning(void) {
    respawnX = -1.0f; respawnY = -1.0f;
    reset_run();
}

static void reset_run(void) {
    if (respawnX > 0.0f && respawnY > 0.0f) {
        player.x = roundf(respawnX - PLAYER_HB_W * 0.5f);
        player.y = roundf(respawnY - PLAYER_HB_H * 0.5f);
    } else if (startX > 0.0f && startY > 0.0f) {
        player.x = roundf(startX - PLAYER_HB_W * 0.5f);
        player.y = roundf(startY - PLAYER_HB_H * 0.5f);
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
    for (int i = 0; i < MAX_PICKUP_FX; i++) pickupFx[i].active = false;

    // Re-arm every extracted map object at its spawn point (death / restart)
    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        const EntityDef* def = &ENTITY_DEFS[e->kind];
        e->active = true;
        e->x = e->sx; e->y = e->sy;
        if (e->kind == ENT_CHICKEN || e->kind == ENT_RINO) {
            e->vx = 0.0f;  // facing kept from Tiled spawn; update loop drives movement
        } else {
            e->vx = (def->speed > 0.0f) ? (((i & 1) == 0) ? -def->speed : def->speed) : 0.0f;
            e->facing = e->vx >= 0.0f ? 1 : -1;
        }
        e->vy = 0.0f;
        e->grounded = false;
        e->activated = false;
        e->animT = 0.0f;
        e->atkT = 0.0f; e->coolT = 0.0f; e->attacking = false; e->fired = false;
        e->animState = (e->kind == ENT_BAT) ? 0 : 1; e->stateT = 0.0f; e->deathT = 0.0f;  // bat back to perch, rest to run, clear Hit
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

// Instantiate one map-placed object at world coords (map px * entityScale).
// Returns the pool slot, or NULL when full / kind invalid (so the caller can
// tweak per-object settings like Tiled facing).
static Entity* spawn_entity(EntityKind kind, float wx, float wy) {
    // return nothing if entityCount more than 72 (max pool size) or kind invalid.
    // entityCount is set to 0 initially (line 217)
    if (entityCount >= MAX_ENTITIES || kind >= ENT_COUNT) return NULL;

    // Look up kind's template (sprite path, cell size, hitbox, speed) from ENTITY_DEFS table
    // e.g. { "Assets/Items/Fruits/Kiwi.png",  NULL, 32, 32, 0.65f, 0.65f, false, true,  false,   0.0f, 12.0f },
    // if kind = ENT_FRUIT_KIWI, then &ENTITY_DEFS[ENT_FRUIT_KIWI] will read above comment.
    const EntityDef* def = &ENTITY_DEFS[kind];

    // point to empty index slot to store kind, alive or not, sprites, pos + respawn + hover baseline, hit box, movement, timers/flags 
    // e.g.  entityCount==0 → e = &entityPool[0], so slot 1 out of 72 is being used. Then entityCount +1 to move next empty slot.
    Entity* e = &entityPool[entityCount++];

    e->kind = kind; // current type (fruits, trap or animals)
    e->active = true; // true = alive (so update/draw/collision can process it)
    e->baseImage = def->path ? CP_Image_Load(def->path) : NULL; // load image's kind sprites (544 x 32 pixels (Kiwi) or 38x38 pixels (Saw))
    e->altImage = def->altPath ? CP_Image_Load(def->altPath) : NULL; // altImage only used by Checkpoint (NoFlag -> Flag)
    // Per-state strips (Chicken/Bat/Rino Idle-Run/Fly-Hit, Plant Hit); NULL = reuse baseImage
    e->idleImg = def->idlePath ? CP_Image_Load(def->idlePath) : NULL;
    e->runImg = def->runPath ? CP_Image_Load(def->runPath) : NULL;
    e->hitImg = def->hitPath ? CP_Image_Load(def->hitPath) : NULL;
    // Bat-only strip (Ceiling In = alert burst on detection); NULL for the rest.
    // (Ceiling Out.png is unused for now — say where you want it, e.g. return-home tuck.)
    if (kind == ENT_BAT) {
        e->emergeImg = CP_Image_Load("Assets/Enemies/Bat/Ceiling In (46x30).png");
    } else {
        e->emergeImg = NULL;
    }
    e->animState = 1; e->stateT = 0.0f; e->deathT = 0.0f;  // start in run/fly state
    e->cellW = def->cellW; // weight of image (e.g. 32)
    e->cellH = def->cellH; // height of image (e.g. 32)
    e->totalFrames = 1;    // updates each frame by 1

    // All fruits images contains sprite. (e.g. Kiwi image contains 544 x 32 pixels)
    // Image sprite is a single image file that combines multiple smaller images.
    // We need to extract small images from image sprite.
    if (e->baseImage) { 
        int iw = CP_Image_GetWidth(e->baseImage); // 544 pixels
        if (iw > def->cellW) e->totalFrames = iw / def->cellW;
    }

    // Current pos = world px passed in (already x*3.0)
    e->x = wx; e->y = wy;
    e->sx = wx; e->sy = wy;

    // Hitbox size = sprite cell * 3.0 scale * fraction, e.g. Kiwi 32*3*0.65
    // The reason why add fraction is because PNG has transparent padding around it, 
    // so hitbox may trigger even when character did not "visually touch" trap or enemies.
    e->w = (float)def->cellW * entityScale * def->hbW;
    e->h = (float)def->cellH * entityScale * def->hbH;

    // Ground markers are authored as points ON the ground surface in Tiled:
    // lift the center so the sprite's feet rest at the point. Without this the
    // middle of the sprite sits at the point and half the checkpoint pole /
    // spike sinks under the tiles (see spike&cp.png).
    if (kind == ENT_START || kind == ENT_CHECKPOINT || kind == ENT_END || kind == ENT_SPIKES) {
        e->y -= (float)def->cellH * entityScale * 0.5f;  // seat feet at the point
        e->sy = e->y;
    }

    // Animals are plotted ~1 tile above the ground in Tiled: drop them so their
    // feet rest exactly on top of the first solid tile (no floating, no half-buried).
    // Needs mapData, so the Map tilelayer must come before Enemies in map.json layers.
    if (mapData && (kind == ENT_CHICKEN || kind == ENT_RINO || kind == ENT_PLANT)) {
        float halfH2 = e->h * 0.5f;
        float bottom = e->y + halfH2;
        if (is_solid_px(e->x, bottom)) {
            // Half-way inside a tile: pop straight up onto its top
            e->y = ((int)(bottom / TILE_SIZE)) * TILE_SIZE - halfH2 - 0.01f;
        } else {
            // Floating: scan down (max 3 tiles) and sit on the first solid top
            for (float d = 4.0f; d <= TILE_SIZE * 3.0f; d += 4.0f) {
                if (is_solid_px(e->x, bottom + d)) {
                    e->y = ((int)((bottom + d) / TILE_SIZE)) * TILE_SIZE - halfH2 - 0.01f;
                    break;
                }
            }
        }
        e->sy = e->y;
        e->vy = 0.0f;
        e->grounded = true;
    }

    // Bat sleeper: tuck into the nearest ceiling (solid tile bottom) above the
    // plotted point, so it hangs off real tiles instead of open air by the
    // window edge. Perch is stored into sx/sy, so reset_run and the RETURN leg
    // both aim at the same ceiling. Needs mapData (Map layer before Enemies).
    if (kind == ENT_BAT && mapData) {
        float surface = 0.0f;  // world-y of the ceiling's bottom edge
        for (float d = 0.0f; d <= TILE_SIZE * 10.0f; d += 4.0f) {
            float py = wy - d;
            if (is_solid_px(wx, py)) { surface = ((int)(py / TILE_SIZE) + 1) * TILE_SIZE; break; }
        }
        if (surface > 0.0f) {
            e->x = wx; e->y = surface + e->h * 0.45f;  // body tucked just under the ceiling
        } else {
            e->x = wx; e->y = wy;  // open sky above: hover where plotted (old behavior)
        }
        e->sx = e->x; e->sy = e->y;
        e->animState = 0;  // start perched on the ceiling (Idle art)
    }

    // Patrol velocity: 
    // 1st ?: if speed>0 (Chicken 90, Rino 130), go to 2nd `?`. Else vx = 0.
    // 2nd ?: alternate -speed/+speed by even/odd count.
    // (Chicken/Rino facing can still be overridden per-object by Tiled object name
    // in parse_object_group_entities, and reset_run preserves it.)
    e->vx = (def->speed > 0.0f) ? (((entityCount & 1) == 0) ? -def->speed : def->speed) : 0.0f;

    // Start with zero velocity speed
    e->vy = 0.0f;

    // Face movement direction (1=right, -1=left for sprite mirror)
    e->facing = e->vx >= 0.0f ? 1 : -1;

    // Reset anim/hover timers and state flags
    // (seated animals above already start grounded = true)
    e->animT = 0.0f;
    if (kind != ENT_CHICKEN && kind != ENT_RINO && kind != ENT_PLANT) e->grounded = false;
    e->activated = false;
    return e;
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

// Bat sleeper tuning (world px; 48px = 1 tile). BAT_AGGRO_RADIUS is the chase
// trigger around the bat — raise for earlier ambushes, lower for later ones.
#define BAT_AGGRO_RADIUS 344.0f  // Wake up bat if player is within 8 tiles.
#define BAT_CHASE_SPEED 150.0f   // world px/s toward the player / back to perch
#define BAT_EMERGE_TIME 0.45f    // Ceiling In alert length before the chase

// Move every living object (gravity + patrol; bats hover; traps stay put)
static void update_entities(float dt) {
    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        e->animT += dt;
        e->stateT += dt;

        // Dying (stomped): freeze, play one-shot Hit strip in draw, then deactivate
        if (e->deathT > 0.0f) {
            e->deathT -= dt;
            if (e->deathT <= 0.0f) e->active = false;
            continue;
        }

        // Hit flash after hurting the PLAYER (stomp-deaths use deathT instead):
        // hold ~0.5s, then fall back to run/fly; per-kind logic below re-picks Idle.
        if (e->animState == 2 && e->stateT > 0.5f) { e->animState = 1; e->stateT = 0.0f; }

        if (def->flies) {
            // Bat sleeper: Idle on the ceiling (state 0) until the player enters
            // BAT_AGGRO_RADIUS, Ceiling In alert burst (one-shot, state 3), chase
            // on the Flying strip (state 1), and drift back to the perch + Idle
            // again when the player leaves. Perch and trigger are tile/player
            // anchored — never the window edge.
            // (state 2 = Hit flash: frozen here, the timer above expires it.)
            float pcx = player.x + PLAYER_HB_W * 0.5f;
            float pcy = player.y + PLAYER_HB_H * 0.5f;
            float dx = pcx - e->x, dy = pcy - e->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (e->animState == 0) {  // perched
                e->vx = 0.0f; e->vy = 0.0f;
                if (dist < BAT_AGGRO_RADIUS && !levelComplete) { e->animState = 3; e->stateT = 0.0f; }
            } else if (e->animState == 3) {  // Ceiling In alert burst
                if (e->stateT > BAT_EMERGE_TIME) { e->animState = 1; e->stateT = 0.0f; }
            } else if (e->animState == 1) {  // flying: chase player, else drift home
                float tx, ty;
                if (dist < BAT_AGGRO_RADIUS * 1.5f && !levelComplete) { tx = pcx; ty = pcy; }
                else { tx = e->sx; ty = e->sy; }  // perch (ceiling), stored at spawn
                float mx = tx - e->x, my = ty - e->y;
                float ml = sqrtf(mx * mx + my * my);
                if (ml > 1.0f) {
                    float nx = e->x + mx / ml * BAT_CHASE_SPEED * dt;
                    float ny = e->y + my / ml * BAT_CHASE_SPEED * dt;
                    // Axis-separated: stop at solid tiles, never enter them
                    if (!is_solid_px(nx + ((mx > 0.0f) ? e->w * 0.5f : -e->w * 0.5f), e->y)) e->x = nx;
                    if (!is_solid_px(e->x, ny + ((my > 0.0f) ? e->h * 0.5f : -e->h * 0.5f))) e->y = ny;
                    e->facing = (mx >= 0.0f) ? 1 : -1;
                }
                // Home under the perch and player gone -> snap in, back to Idle
                float hx = e->sx - e->x, hy = e->sy - e->y;
                if (dist >= BAT_AGGRO_RADIUS * 1.5f && sqrtf(hx * hx + hy * hy) < 10.0f) {
                    e->x = e->sx; e->y = e->sy;
                    e->animState = 0; e->stateT = 0.0f;
                }
            }
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

        // Chicken/Rino: always patrol-walk left and right on the Run strip
        // (no Idle); charge the player on the Run strip when seen;
        // Hit strip flashes right after hurting the player.
        // (The old generic patrol block below is gone: Chicken/Rino were its only
        // walkers — Bat flies and Plant attacks, both handled above.)
        if ((e->kind == ENT_CHICKEN || e->kind == ENT_RINO) && e->grounded) {
            float pcx = player.x + PLAYER_HB_W * 0.5f;
            float pcy = player.y + PLAYER_HB_H * 0.5f;
            float dx = pcx - e->x, dy = pcy - e->y;
            // "in front": player on the facing side, close, roughly level with the enemy
            bool seen = !levelComplete && ((dx * (float)e->facing) > 0.0f)
                     && fabsf(dx) < 480.0f && fabsf(dy) < 110.0f;
            if (e->animState == 2) {
                e->vx = 0.0f;  // impact freeze; the timer above expires the flash
            } else {
                e->animState = 1;  // Run strip always (no Idle)
                if (seen) e->facing = (dx >= 0.0f) ? 1 : -1;  // face the player to charge
                // Patrol-walk when unseen, 2.5x charge when seen (Chicken 225, Rino 325)
                e->vx = (float)e->facing * def->speed * (seen ? 2.5f : 1.0f);
                // Step forward: reverse at walls/ledges on patrol, stop on charge
                // (no reverse, no suicide jumps)
                float nx = e->x + e->vx * dt;
                float fx = (e->vx > 0.0f) ? nx + e->w * 0.5f : nx - e->w * 0.5f;
                bool wall = is_solid_px(fx, e->y - halfH * 0.6f) || is_solid_px(fx, e->y + 2.0f);
                float ahead = nx + ((e->vx > 0.0f) ? (e->w * 0.5f + 4.0f) : -(e->w * 0.5f + 4.0f));
                bool ledge = !is_solid_px(ahead, e->y + halfH + 6.0f);
                if (wall || ledge) {
                    if (seen) { /* charge stops */ }
                    else { e->vx = -e->vx; e->facing = e->vx > 0.0f ? 1 : -1; }
                } else {
                    e->x = nx;
                }
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
        if (e->deathT > 0.0f) continue;  // already stomped: Hit strip playing, no more hits

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
                spawn_pickup_fx(e->x, e->y);  // Collected sparkle at the fruit's center
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
            if (e->hitImg) {
                // Stomped: freeze + play one-shot Hit strip, removed when deathT hits 0
                e->animState = 2; e->stateT = 0.0f; e->deathT = 0.45f;
                e->vx = 0.0f; e->vy = 0.0f;
                e->attacking = false; e->fired = false;
            } else {
                e->active = false;
            }
            if (playerHitSnd) CP_Sound_Play(playerHitSnd);  // player stomped an enemy
            for (int k = 0; k < 5; k++)
                spawn_dust(e->x + (float)((k % 3) - 1) * 14.0f, e->y - e->h * 0.2f,
                           (float)((k % 3) - 1) * 90.0f, -140.0f, 0.45f, 18.0f);
            player.vy = -470.0f;
            player.jumps = 1;
            player.doubleAnim = false;
        } else {
            // Enemy hurt the PLAYER (not a stomp): flash its Hit strip briefly.
            // Rino Hit (52x34).png triggers here; stomp-death is the branch above.
            if (e->hitImg) { e->animState = 2; e->stateT = 0.0f; }
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
        bool plantFiring = (e->kind == ENT_PLANT && e->attacking && e->deathT <= 0.0f && plantAtkImg);
        if (plantFiring) img = plantAtkImg;
        // Multi-state enemies (Bat Idle/Fly + Chicken/Rino/Bat/Plant Hit): pick strip
        // by animState, then recount frames from that strip's width (strips differ
        // in length). Skipped while the Plant fires so the Attack strip always wins.
        // (Found by audit: without the !plantFiring guard the baseImage fallback
        // below stole the image back and the Attack art never showed.)
        bool hasStates = !plantFiring && (e->idleImg || e->runImg || e->hitImg);
        // Bat sleeper strips: 0 = Idle while perched on the ceiling,
        // 3 = Ceiling In alert burst on detection, 1 = chase/return (Flying),
        // 2 = Hit. Falls back to Flying if a file is missing.
        if (!plantFiring && e->kind == ENT_BAT && e->emergeImg) {
            if (e->animState == 2 && e->hitImg) img = e->hitImg;
            else if (e->animState == 3 && e->emergeImg) img = e->emergeImg;
            else if (e->animState == 0 && e->idleImg) img = e->idleImg;
            else if (e->runImg) img = e->runImg;
            else if (e->baseImage) img = e->baseImage;
        }
        else if (hasStates) {
            if (e->animState == 2 && e->hitImg) img = e->hitImg;
            else if (e->animState == 0 && e->idleImg) img = e->idleImg;
            else if (e->animState == 1 && e->runImg) img = e->runImg;
            else if (e->baseImage) img = e->baseImage;
        }
        if (!img) continue;

        int frames = e->totalFrames;
        if (hasStates && img != e->baseImage && img != plantAtkImg) {
            int iw2 = CP_Image_GetWidth(img);
            frames = (iw2 > e->cellW) ? iw2 / e->cellW : 1;
        }
        if (e->kind == ENT_CHECKPOINT)
            frames = e->activated ? (CP_Image_GetWidth(img) / e->cellW) : 1;
        if (plantFiring) frames = 8;  // Attack strip
        if (frames < 1) frames = 1;

        float animFps = (e->kind == ENT_CHECKPOINT && e->activated) ? 10.0f : def->animFps;
        if (plantFiring) animFps = 12.0f;
        int frame = (frames > 1) ? (((int)(e->animT * animFps)) % frames) : 0;
        if (plantFiring) { frame = (int)(e->atkT * 12.0f); if (frame > 7) frame = 7; }  // synced to release
        // Hit is one-shot (PixelAdventure style): hold on the last frame until deathT removes it
        if (hasStates && e->animState == 2) {
            frame = (int)(e->stateT * 10.0f);
            if (frame > frames - 1) frame = frames - 1;
            if (frame < 0) frame = 0;
        }

        // Fruits are drawn at the tile footprint, like their map objects
        bool isFruit = e->kind >= ENT_FRUIT_ORANGE && e->kind <= ENT_FRUIT_STRAWBERRY;
        float dw = isFruit ? TILE_SIZE : (float)e->cellW * entityScale;
        float dh = isFruit ? TILE_SIZE : (float)e->cellH * entityScale;
        float sx = roundf(e->x - dw * 0.5f - camX);
        float sy = roundf(e->y - dh * 0.5f - camY);
        // Chicken/Rino/Plant art is taller than its hitbox, so center-drawing sank
        // their legs into tiles (see animal.png). Anchor these at the feet instead:
        // sprite bottom = hitbox bottom, which physics keeps on the tile top.
        // (Checkpoints/Spikes were fixed the other way: lifted at spawn + centered draw.)
        if (e->kind == ENT_CHICKEN || e->kind == ENT_RINO || e->kind == ENT_PLANT)
            sy = roundf(e->y + e->h * 0.5f - dh - camY);
        if (sx < -192.0f || sx > SCREEN_WIDTH + 192.0f ||
            sy < -192.0f || sy > SCREEN_HEIGHT + 192.0f) continue;

        // Rino/Chicken/Plant strips are authored facing left: mirror them so
        // they face the direction they move (or shoot) in
        int dfacing = e->facing;
        if (e->kind == ENT_RINO || e->kind == ENT_CHICKEN || e->kind == ENT_PLANT) dfacing = -dfacing;
        draw_sheet_frame(img, e->cellW, e->cellH, frame, sx, sy, dw, dh, dfacing, 255);
    }
}

// Extract objects of one objectgroup leaf (e.g. "Kiwi") into entity pool
static void parse_object_group_entities(cJSON* layer) {
    // Get "name" field from this layer dict, e.g. {"name":"Kiwi","type":"objectgroup","objects":[...]}
    // where "objects":[...] contains [{"x":1310,"y":867},{"x":1717,"y":889}...]
    cJSON* nameItem = cJSON_GetObjectItemCaseSensitive(layer, "name"); // nameItem = "Kiwi"

    // Safely extract C-string; if missing/non-string use "" to avoid NULL crash
    // e.g. return "" if nameItem = "".
    const char* name = (cJSON_IsString(nameItem) && nameItem->valuestring) ? nameItem->valuestring : "";

    // Map leaf name -> EntityKind via kind_from_layer_name()
    // e.g. "Kiwi"->ENT_FRUIT_KIWI, "Saw"->ENT_SAW,
    EntityKind kind = kind_from_layer_name(name);
    if (kind >= ENT_COUNT) return;   // unknown name returns ENT_COUNT -> skip (nothing to spawn)

    // Get "objects position" array from this leaf
    // e.g. Multiple Kiwi fruits have positions of [{"x":1310,"y":867},{"x":1717,"y":889}...]
    cJSON* objects = cJSON_GetObjectItemCaseSensitive(layer, "objects");
    if (!cJSON_IsArray(objects)) return;

    // Count how many kiwi fruits placed in tiled eidtor.
    // e.g. [{"x":1310,"y":867},{"x":1717,"y":889}] = 2 kiwi fruits.
    int n = cJSON_GetArraySize(objects);

    // For each Kiwi, get it's X & Y coordinates
    for (int i = 0; i < n; i++) {
        // Get i-th point dict, e.g. {"x":1310,"y":867,"point":true}
        cJSON* obj = cJSON_GetArrayItem(objects, i);
        if (!obj) continue;

        // Get object coordinates (Kiwi position at "x":1310,"y":867).
        cJSON* x = cJSON_GetObjectItemCaseSensitive(obj, "x");
        cJSON* y = cJSON_GetObjectItemCaseSensitive(obj, "y");

        // spawn one entity at world pos = Tiled px * 3.0 to match 16px->48px map; (or else fruits would spawn off scaled map)
        // size is scaled inside spawn_entity()
        if (cJSON_IsNumber(x) && cJSON_IsNumber(y)) {
            // Format: spawn_entity(kind, x position, y position);
            Entity* se = spawn_entity(kind, (float)x->valuedouble * entityScale, (float)y->valuedouble * entityScale);
            // Initial facing for Chicken/Rino comes from the OBJECT name in Tiled
            // (layer name "Chicken" picks the kind; point name picks facing):
            // select the point -> Properties -> Name -> "left" or "right".
            // Blank name = alternating default. This answers "which chicken faces where".
            if (se && (kind == ENT_CHICKEN || kind == ENT_RINO)) {
                cJSON* oname = cJSON_GetObjectItemCaseSensitive(obj, "name");
                const char* on = (cJSON_IsString(oname) && oname->valuestring) ? oname->valuestring : "";
                if (strstr(on, "left")) se->facing = -1;
                else if (strstr(on, "right")) se->facing = 1;
                se->vx = 0.0f;  // update loop drives vx from facing from now on
            }
        }
    }
}

// Get map tile layer (Grid map structure built by `tileSheet` + obj location points groups)
static void process_layers_world(cJSON* layers) {
    if (!cJSON_IsArray(layers)) return;

    // Find number of layers (n) (1 layer = map Structure, 2nd layer = fruit objects location)
    int n = cJSON_GetArraySize(layers);
    for (int l = 0; l < n; l++) {
        // (1st loop) layers[0] -> 1st item. (2nd loop) layers[1] -> 2nd item.
        cJSON* layer = cJSON_GetArrayItem(layers, l);
        if (!layer) continue;

        // "type": "tilelayer" (refers to main map design) or "type": "objectgroup" (refers to object props in map)
        cJSON* type = cJSON_GetObjectItemCaseSensitive(layer, "type");

        // skip loop if type (layer) does not exist or type contains no string 
        if (!cJSON_IsString(type) || !type->valuestring) continue;
        
        // Get map structure:
        // string == "tilelayer" (map design level)
        // strcmp(A, B) == 0 means True.
        if (strcmp(type->valuestring, "tilelayer") == 0) {
            cJSON* data = cJSON_GetObjectItemCaseSensitive(layer, "data");
            if (cJSON_IsArray(data)) {
                int dataSize = cJSON_GetArraySize(data); // No. of GIDs
                if (mapData) free(mapData);
                mapData = (int*)malloc(sizeof(int) * dataSize); // buffer
                if (mapData == NULL) {
                    // if Memory allocation failed, return nothing.
                    return;
                }

                // Copy each GID into `mapData` buffer
                for (int i = 0; i < dataSize; i++) {
                    // Get GIDs tile number.
                    cJSON* tile = cJSON_GetArrayItem(data, i);
                    // Convert from JSON format to C lanuage format.
                    if (cJSON_IsNumber(tile)) mapData[i] = tile->valueint;
                }
            }
        }
        // objectgroup (contains fruits (kiwi, melon etc), enemies (plant, chicken etc), checkpoints (start, end)), trap (saw, spikes)
        else if (strcmp(type->valuestring, "objectgroup") == 0) {
            parse_object_group_entities(layer);
        }
        // group contain Folders of Fruit, Enemies, Trap, Checkpoint.
        else if (strcmp(type->valuestring, "group") == 0) {
            cJSON* sub = cJSON_GetObjectItemCaseSensitive(layer, "layers");
            if (cJSON_IsArray(sub)) process_layers_world(sub);
        }
    }
}

// Loads and parses map.json exported from Tiled using cJSON
static void load_map_json(const char* filepath) {
    FILE* file = fopen(filepath, "rb"); //default path is at OutDir/Assets/Terrain/map.json

    // Create a buffer (that can hold entire map.json size):
    fseek(file, 0, SEEK_END); // moves file current position to file's end (to get total size of file)
    long length = ftell(file); // ftell =  gives the current position in bytes
    fseek(file, 0, SEEK_SET); // moves file end position back to start position.
    char* buffer = (char*)malloc((size_t)length + 1); // +1 to put null terminator `\0'` at the end of buffer.

    // Copies map.json file content into buffer
    // buffer now contains raw JSON text structure
    // buffer = // [ JSON ][ JSON ] ... [ JSON ][ ? ] (last byte is empty)
    fread(buffer, 1, length, file);
    buffer[length] = '\0'; // place `\0'` at the end of buffer. This indicates end of the C string.
    fclose(file);

    // cJSON take & understand this raw JSON text structure so that you can obtain dimensions (width, height) etc.
    cJSON* json = cJSON_Parse(buffer);
    free(buffer);
    if (!json) return;

    // Get map dimensions
    cJSON* width = cJSON_GetObjectItemCaseSensitive(json, "width"); //"width": 200 
    cJSON* height = cJSON_GetObjectItemCaseSensitive(json, "height"); //"height": 100
    cJSON* tilew = cJSON_GetObjectItemCaseSensitive(json, "tilewidth"); //"tilewidth": 16
    cJSON* tileh = cJSON_GetObjectItemCaseSensitive(json, "tileheight"); //"tileheight": 16

    // Sometimes after getting dimensions (width etc) does not guarantee a number.
    // Thus, we do `(cJSON_IsNumber(width))` to check if it is a number.
    // We convert JSON format `"width": 200` to C format `mapWidth = 200`
    if (cJSON_IsNumber(width)) mapWidth = width->valueint;
    if (cJSON_IsNumber(height)) mapHeight = height->valueint;
    if (cJSON_IsNumber(tilew)) tileWidth = tilew->valueint;
    if (cJSON_IsNumber(tileh)) tileHeight = tileh->valueint;

    cJSON* layers = cJSON_GetObjectItemCaseSensitive(json, "layers");
    if (cJSON_IsArray(layers)) {
        process_layers_world(layers);   // tile layer + objectgroups (incl. nested groups)
    }

    cJSON_Delete(json);

    {  // per-kind breakdown so map points can be verified in the console (CP_Engine_Run(1); to show below printf message)
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
    CP_Settings_ImageMode(CP_POSITION_CORNER);

    // Pre-rendered map (like PixelAdventure's map.png): 1 draw of the camera's
    // sub-rect. map.png is a 1x Tiled export (3200x1600), so map-px = world-px / entityScale.
    // mapData/map.json is still loaded and used for collision only.
    if (mapImage) {
        float s = (entityScale > 0.0f) ? entityScale : 3.0f;
        float sx0 = camX / s, sy0 = camY / s;
        float sw = SCREEN_WIDTH / s, sh = SCREEN_HEIGHT / s;
        CP_Image_DrawSubImage(mapImage, 0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT,
                              sx0, sy0, sx0 + sw, sy0 + sh, 255);
        return;
    }

    // Fallback: per-tile render from the tilesheet map.json (only if map.png missing)
    if (!mapData || !terrainSheet) return;
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

// (Title-screen UI moved to menu.c: draw_menu_ui, quit popup, menu input.)

// -----------------------------------------------------------------------------
// Lifecycle Functions
// -----------------------------------------------------------------------------

void game_init(void) {
    // Window + menu/shared assets are owned by menu_init (menu always runs first).
    // This loads only gameplay assets here (guards skip anything already loaded,
    // since quitting to menu and back re-enters this state).
    CP_Settings_ImageMode(CP_POSITION_CORNER);
    CP_Settings_ImageFilterMode(CP_IMAGE_FILTER_NEAREST);

    // Full gameplay loads (game_exit frees + NULLs everything on every exit,
    // so each entry reloads cleanly with no leaks or duplicated entities).
    restartBtnImage = CP_Image_Load("Assets/Menu/Buttons/Restart.png");

    terrainSheet = CP_Image_Load("Assets/Terrain/Terrain (16x16).png");
    if (terrainSheet) {
        sheetW = (float)CP_Image_GetWidth(terrainSheet);
        sheetH = (float)CP_Image_GetHeight(terrainSheet);
    }

    // Pre-rendered map (Tiled -> Export as Image). One draw per frame; tile loop below is fallback.
    mapImage = CP_Image_Load("Assets/Terrain/map.png");
    if (mapImage) printf("Map image loaded: %dx%d\n", CP_Image_GetWidth(mapImage), CP_Image_GetHeight(mapImage));
    else printf("Map image missing, falling back to tile render\n");

    load_map_json("Assets/Terrain/map.json");

    // New run starts at the first "Start" checkpoint placed in the map.
    // Stored separately from the checkpoint respawn so fresh starts survive
    // restart_from_beginning() clearing respawnX/Y (that wipe caused the
    // player to fall back to find_spawn_cell and drop out of the map).
    for (int i = 0; i < entityCount; i++) {
        if (entityPool[i].kind == ENT_START) {
            startX = entityPool[i].x;
            startY = entityPool[i].y;
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
    collectedImg = CP_Image_Load("Assets/Items/Fruits/Collected.png");  // 32x32 x6 pickup sparkle

    // Audio: streamed music + one-shot SFX
    // (menuMusic/clickSnd/windowOpenSnd are menu-owned in menu_init — not reloaded here)
    worldMusic = CP_Sound_LoadMusic("Assets/Sound/world_music.wav");
    stepSnd = CP_Sound_Load("Assets/Sound/step.wav");
    jumpSnd = CP_Sound_Load("Assets/Sound/jump.wav");
    respawnSnd = CP_Sound_Load("Assets/Sound/respawn.wav");
    playerHitSnd = CP_Sound_Load("Assets/Sound/player_hit.wav");
    hitSnd = CP_Sound_Load("Assets/Sound/hit.wav");
    deathSnd = CP_Sound_Load("Assets/Sound/death.wav");
    coinSnd = CP_Sound_Load("Assets/Sound/coin.wav");
    plantAtkImg = CP_Image_Load("Assets/Enemies/Plant/Attack (44x42).png");
    bulletImg = CP_Image_Load("Assets/Enemies/Plant/Bullet.png");
    piecesImg = CP_Image_Load("Assets/Enemies/Plant/Bullet Pieces.png");
    apply_volumes();

    restart_from_beginning();  // fresh run on every entry (Game_Init runs per entry now)
}

// Pause menu rows (page 0: main, page 1: audio). Row i center-y = 310 + i * 70 (design space).
static float pause_row_y(int i) { return 310.0f + (float)i * 70.0f; }

static void draw_pause_menu(void) {
    // Dark overlay over the frozen world (PixelAdventure uses 0,0,0,128)
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    // Banner
    draw_cream_panel(SCREEN_WIDTH * 0.5f, 140.0f, 420.0f, 96.0f); // defined in utils.c
    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(54.0f);
    CP_Font_DrawText(pausePage == 0 ? "PAUSED" : "OPTIONS", SCREEN_WIDTH * 0.5f, 140.0f);

    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();

    // Top panel
    draw_cream_panel(SCREEN_WIDTH * 0.5f, 380.0f, 440.0f, 300.0f);
    CP_Settings_TextSize(40.0f);
    if (pausePage == 0) {
        const char* rows[] = { "RESUME", "RESTART", "OPTIONS" };
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            // bool hover = mx >= 450.0f && mx <= 830.0f && my >= ry - 28.0f && my <= ry + 28.0f;
            if (is_mouse_over(450.0f, ry - 28.0f, 380.0f, 56.0f)) {
                CP_Settings_RectMode(CP_POSITION_CORNER);
                CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
                CP_Graphics_DrawRect(450.0f, ry - 28.0f, 380.0f, 56.0f);
            }
            CP_Settings_Fill(is_mouse_over(450.0f, ry - 28.0f, 380.0f, 56.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
            CP_Font_DrawText(rows[i], SCREEN_WIDTH * 0.5f, ry);
        }
    }
    else {
        // Audio rows: label + [-] value [+]
        const char* rows[] = { "MUSIC", "SFX", "BACK" };
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            if (i < 2) {
                // Audio row centered on the panel (panel spans 420-860, center 640):
                // label 490 | [-] 606-654 | value 698 | [+] 742-789.
                float vol = (i == 0) ? musicVol : sfxVol;
                bool hovMinus = mx >= 606.0f && mx <= 654.0f && my >= ry - 24.0f && my <= ry + 24.0f;
                bool hovPlus = mx >= 742.0f && mx <= 790.0f && my >= ry - 24.0f && my <= ry + 24.0f;
                CP_Settings_RectMode(CP_POSITION_CORNER);
                CP_Settings_Fill(hovMinus ? CP_Color_Create(215, 140, 40, 160) : CP_Color_Create(40, 40, 40, 40));
                CP_Graphics_DrawRect(606.0f, ry - 24.0f, 48.0f, 48.0f);
                CP_Settings_Fill(hovPlus ? CP_Color_Create(215, 140, 40, 160) : CP_Color_Create(40, 40, 40, 40));
                CP_Graphics_DrawRect(742.0f, ry - 24.0f, 48.0f, 48.0f);
                CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
                CP_Font_DrawText("-", 630.0f, ry);
                CP_Font_DrawText("+", 766.0f, ry);
                char vbuf[16];
                sprintf(vbuf, "%d%%", (int)(vol * 100.0f + 0.5f));
                CP_Font_DrawText(vbuf, 698.0f, ry);
                CP_Font_DrawText(rows[i], 490.0f, ry);
            }
            else {
                bool hover = mx >= 450.0f && mx <= 830.0f && my >= ry - 28.0f && my <= ry + 28.0f;
                if (hover) {
                    CP_Settings_RectMode(CP_POSITION_CORNER);
                    CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
                    CP_Graphics_DrawRect(450.0f, ry - 28.0f, 380.0f, 56.0f);
                }
                CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
                CP_Font_DrawText(rows[i], SCREEN_WIDTH * 0.5f, ry);
            }
        }
    }

    // Bottom panel: MENU | QUIT (PixelAdventure has Menu + Desktop)
    draw_cream_panel(SCREEN_WIDTH * 0.5f, 600.0f, 440.0f, 100.0f);
    bool hovMenu = mx >= 430.0f && mx <= 630.0f && my >= 572.0f && my <= 628.0f;
    bool hovQuit = mx >= 650.0f && mx <= 850.0f && my >= 572.0f && my <= 628.0f;
    CP_Settings_RectMode(CP_POSITION_CORNER);
    if (hovMenu) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(430.0f, 572.0f, 200.0f, 56.0f);
    }
    if (hovQuit) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(650.0f, 572.0f, 200.0f, 56.0f);
    }
    CP_Settings_Fill(hovMenu ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("MENU", 530.0f, 600.0f);
    CP_Settings_Fill(hovQuit ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("QUIT", 750.0f, 600.0f);
}

static void handle_pause_input(void) {
    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();
    bool clicked = CP_Input_MouseClicked();
    if (pausePage == 0) {
        if (!clicked) return;
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            if (mx >= 450.0f && mx <= 830.0f && my >= ry - 28.0f && my <= ry + 28.0f) {
                if (i == 0) paused = false;                    // RESUME
                else if (i == 1) { restart_from_beginning(); paused = false; }  // RESTART: from beginning
                else { pausePage = 1; if (windowOpenSnd) CP_Sound_Play(windowOpenSnd); }  // OPTIONS
                play_click();
                return;
            }
        }
        if (mx >= 430.0f && mx <= 630.0f && my >= 572.0f && my <= 628.0f) {
            CP_Engine_SetNextGameState(menu_init, menu_update, menu_exit);  // MENU: back to title
            paused = false;
            if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            play_click();
        }
        else if (mx >= 650.0f && mx <= 850.0f && my >= 572.0f && my <= 628.0f) {
            CP_Engine_Terminate();      // QUIT: close the game
        }
    }
    else {
        if (!clicked) return;
        for (int i = 0; i < 2; i++) {
            float ry = pause_row_y(i);
            float* vol = (i == 0) ? &musicVol : &sfxVol;
            if (mx >= 606.0f && mx <= 654.0f && my >= ry - 24.0f && my <= ry + 24.0f) {
                *vol -= 0.1f; if (*vol < 0.0f) *vol = 0.0f;
                apply_volumes();
                play_click();
                return;
            }
            if (mx >= 742.0f && mx <= 790.0f && my >= ry - 24.0f && my <= ry + 24.0f) {
                *vol += 0.1f; if (*vol > 1.0f) *vol = 1.0f;
                apply_volumes();
                if (i == 1 && stepSnd) CP_Sound_Play(stepSnd);
                else play_click();
                return;
            }
        }
        float ry = pause_row_y(2);
        if (mx >= 450.0f && mx <= 830.0f && my >= ry - 28.0f && my <= ry + 28.0f) { pausePage = 0; play_click(); }  // BACK
        if (mx >= 430.0f && mx <= 630.0f && my >= 572.0f && my <= 628.0f) {
            CP_Engine_SetNextGameState(menu_init, menu_update, menu_exit);  // MENU: back to title
            paused = false;
            if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            play_click();
        }
        else if (mx >= 650.0f && mx <= 850.0f && my >= 572.0f && my <= 628.0f) {
            CP_Engine_Terminate();
        }
    }
}

void game_update(void) {
    // Keep the logical screen in sync with the fullscreen (or resized) window
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // Game state only now (menu lives in menu.c; the engine switches states,
    // so the old STATE_MENU / STATE_GAME switch is gone).
    {
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
            bgScroll += BG_SCROLL_SPEED * dt;
            if (musicMode != 2 && worldMusic) {
                CP_Sound_StopGroup(CP_SOUND_GROUP_MUSIC);
                CP_Sound_PlayAdvanced(worldMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
                musicMode = 2;
            }

            if (!levelComplete) {
            // --- Input: A/D move, SPACE jump/double (single move speed) ---
            bool left = CP_Input_KeyDown(KEY_A);
            bool right = CP_Input_KeyDown(KEY_D);
            if (player.lockT > 0.0f) player.vx = player.kbx;
            else if (left && !right) { player.vx = -MOVE_SPEED; player.facing = -1; }
            else if (right && !left) { player.vx = MOVE_SPEED; player.facing = 1; }
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

            // --- Footsteps + run dust ---
            if (player.grounded && (player.vx > 20.0f || player.vx < -20.0f)) {
                player.stepT += dt;
                if (player.stepT >= 0.30f) {
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

            // --- Pickup sparkles (Collected one-shot, 6 frames then slot frees) ---
            for (int i = 0; i < MAX_PICKUP_FX; i++) {
                if (!pickupFx[i].active) continue;
                pickupFx[i].t += dt;
                if (pickupFx[i].t >= (float)PICKUP_FRAMES / PICKUP_FPS) pickupFx[i].active = false;
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

        // Pickup sparkle (Collected one-shot, drawn at the fruit's TILE_SIZE footprint)
        if (collectedImg) {
            for (int i = 0; i < MAX_PICKUP_FX; i++) {
                if (!pickupFx[i].active) continue;
                int frame = (int)(pickupFx[i].t * PICKUP_FPS);
                if (frame > PICKUP_FRAMES - 1) frame = PICKUP_FRAMES - 1;
                float sx = roundf(pickupFx[i].x - TILE_SIZE * 0.5f - camX);
                float sy = roundf(pickupFx[i].y - TILE_SIZE * 0.5f - camY);
                if (sx < -64.0f || sx > SCREEN_WIDTH + 64.0f ||
                    sy < -64.0f || sy > SCREEN_HEIGHT + 64.0f) continue;
                draw_sheet_frame(collectedImg, 32, 32, frame, sx, sy, TILE_SIZE, TILE_SIZE, 1, 255);
            }
        }

        // HUD: hearts row (top-left)
        if (heartImg) {
            for (int i = 0; i < MAX_HEARTS; i++)
                draw_sheet_frame(heartImg, 16, 16, (i < hearts) ? 0 : 1, 16.0f + (float)i * 48.0f, 14.0f, 48.0f, 48.0f, 1, 255);
        }

        // Level complete overlay (End object reached)
        if (levelComplete) {
            draw_cream_panel(SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f, 560.0f, 280.0f);
            if (gameFont) CP_Font_Set(gameFont);
            CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
            CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
            CP_Settings_TextSize(58.0f);
            CP_Font_DrawText("LEVEL COMPLETE", SCREEN_WIDTH * 0.5f, 310.0f);
            CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
            CP_Settings_TextSize(30.0f);
            CP_Font_DrawText("Click anywhere to return to menu", SCREEN_WIDTH * 0.5f, 400.0f);
            if (CP_Input_MouseClicked()) {
                CP_Engine_SetNextGameState(menu_init, menu_update, menu_exit);  // back to title
                paused = false;
                levelComplete = false;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            }
        }

        if (!paused) {
                // Restart button (top-right): click = fresh run from the beginning
                if (restartBtnImage) {
                    float rx = SCREEN_WIDTH - 64.0f;
                    float ry = 16.0f;
                    float rsize = 48.0f;
                    bool hover = is_mouse_over(rx, ry, rsize, rsize);
                    float rs = hover ? rsize + 6.0f : rsize;
                    float ro = (rs - rsize) * 0.5f;
                    CP_Image_Draw(restartBtnImage, rx - ro, ry - ro, rs, rs, 255);
                    if (hover && CP_Input_MouseClicked()) { play_click(); restart_from_beginning(); }
                }

            // Options button (top-right, left of restart): opens the pause menu
            if (settingsBtnImage) {
                float sx = SCREEN_WIDTH - 64.0f - 56.0f;
                float sy = 16.0f;
                float ssize = 48.0f;
                bool hover = is_mouse_over(sx, sy, ssize, ssize);
                float ss = hover ? ssize + 6.0f : ssize;
                float so = (ss - ssize) * 0.5f;
                CP_Image_Draw(settingsBtnImage, sx - so, sy - so, ss, ss, 255);
                if (hover && CP_Input_MouseClicked()) {
                    paused = true; pausePage = 0;
                    if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                    play_click();
                }
            }
        }
        else {
            draw_pause_menu();
            handle_pause_input();
        }
    }
}

void game_exit(void) {
    // Free allocated memory on shutdown. menu_exit runs on every menu->game
    // switch (menu can be revisited) so it frees nothing — menu-owned art
    // goes through menu_free_assets() here, once, at terminate.
    // NOTE: game_exit ALSO runs on every quit-to-menu (engine exits the old
    // state on switch), so every pointer freed here is set back to NULL and
    // entityCount to 0. Otherwise menu_init/game_init reload guards see stale
    // non-NULL handles, skip reloading, and the menu draws freed memory
    // (that use-after-free was the finish-game crash).
    menu_free_assets();  // play/vol buttons (menu-owned; NULLs itself)
    if (bgImage) CP_Image_Free(bgImage);
    if (gameFont) CP_Font_Free(gameFont);
    bgImage = NULL; gameFont = NULL;

    if (settingsBtnImage) CP_Image_Free(settingsBtnImage);
    if (restartBtnImage) CP_Image_Free(restartBtnImage);
    settingsBtnImage = NULL; restartBtnImage = NULL;
    if (terrainSheet) CP_Image_Free(terrainSheet);
    if (mapImage) CP_Image_Free(mapImage);
    terrainSheet = NULL; mapImage = NULL;
    if (playerIdle) CP_Image_Free(playerIdle);
    if (playerRun) CP_Image_Free(playerRun);
    if (playerJump) CP_Image_Free(playerJump);
    if (playerDouble) CP_Image_Free(playerDouble);
    if (playerFall) CP_Image_Free(playerFall);
    playerIdle = NULL; playerRun = NULL; playerJump = NULL; playerDouble = NULL; playerFall = NULL;
    if (shadowImg) CP_Image_Free(shadowImg);
    if (dustImg) CP_Image_Free(dustImg);
    if (heartImg) CP_Image_Free(heartImg);
    if (collectedImg) CP_Image_Free(collectedImg);
    shadowImg = NULL; dustImg = NULL; heartImg = NULL; collectedImg = NULL;
    for (int i = 0; i < entityCount; i++) {
        if (entityPool[i].baseImage) CP_Image_Free(entityPool[i].baseImage);
        if (entityPool[i].altImage) CP_Image_Free(entityPool[i].altImage);
        if (entityPool[i].idleImg) CP_Image_Free(entityPool[i].idleImg);
        if (entityPool[i].runImg) CP_Image_Free(entityPool[i].runImg);
        if (entityPool[i].hitImg) CP_Image_Free(entityPool[i].hitImg);
        if (entityPool[i].emergeImg) CP_Image_Free(entityPool[i].emergeImg);
        entityPool[i].baseImage = NULL; entityPool[i].altImage = NULL;
        entityPool[i].idleImg = NULL; entityPool[i].runImg = NULL;
        entityPool[i].hitImg = NULL; entityPool[i].emergeImg = NULL;
    }
    entityCount = 0;  // spawn pool restarts empty on next entry
    if (plantAtkImg) CP_Image_Free(plantAtkImg);
    if (bulletImg) CP_Image_Free(bulletImg);
    if (piecesImg) CP_Image_Free(piecesImg);
    plantAtkImg = NULL; bulletImg = NULL; piecesImg = NULL;
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
    playerHitSnd = NULL; hitSnd = NULL; deathSnd = NULL; coinSnd = NULL;
    menuMusic = NULL; worldMusic = NULL; stepSnd = NULL; jumpSnd = NULL;
    respawnSnd = NULL; clickSnd = NULL; windowOpenSnd = NULL;
    if (mapData) free(mapData);
    mapData = NULL; mapWidth = 0; mapHeight = 0;
}