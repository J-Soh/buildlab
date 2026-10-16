// memory leak checker
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cprocessing.h"
#include "utils.h"  // shared canvas, assets, UI helpers (screen vars, font, sounds, cream panel)
#include "menu.h"   // menu_init/menu_update/menu_exit (pause MENU + level-complete return here)
#include "game.h"   // own state entry points + shared render state (mapImage, camX/camY...)
#include "extract.h" // map extraction: entity types/pool, load_map_json, render_map_json
#include "pause.h"   // pause menu state (paused/pausePage) + draw/handle entry points

// Gameplay HUD button (menu buttons moved to menu.c;
// shared canvas/assets/helpers moved to utils.c)
static CP_Image restartBtnImage = NULL;  // gameplay HUD, top-right

// Corner buttons use a fixed 1600x900 top-right layout (48px, no scaling).

// Pause state (paused/pausePage) now owned by pause.c (externs in pause.h) —
// the game loop below reads/writes the flags, pause.c draws/handles the menu.

// Terrain Tilesheet Handle & Dimensions (fallback if map.png is missing;
// shared with render_map_json in extract.c via game.h)
CP_Image terrainSheet = NULL;
float sheetW = 384.0f; // Width of Terrain (16x16).png
static float sheetH = 192.0f; // Height of Terrain (16x16).png

// Pre-rendered map (Tiled -> Export as Image at 1x = 3200x1600).
// Drawn as one camera sub-rect per frame; map.json tile data stays for collision only.
// (Shared with render_map_json in extract.c via game.h; freed in game_exit.)
CP_Image mapImage = NULL;

// Tilemap Camera (world-pixel position of the viewport's top-left corner;
// shared with render_map_json in extract.c via game.h)
float camX = 0.0f;
float camY = 0.0f;
static const float CAM_SPEED = 500.0f; // world px per second

// Map tile px now owned by extract.c (tileWidth/tileHeight externs in extract.h;
// load_map_json sets them, render_map_json reads them).

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
CP_Sound stepSnd = NULL;  // shared with pause.c SFX preview (extern in game.h)
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

// Entity types, ENTITY_DEFS table, entity pool + entityScale now owned by
// extract.c (externs in extract.h) — extraction fills them, gameplay reads them.

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
void restart_from_beginning(void) {
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

    // Re-arm every extracted map object (chicken, bat, trap etc) at its spawn point (death / restart)
    // vx = velocity x-axis movement
    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        const EntityDef* def = &ENTITY_DEFS[e->kind];
        e->active = true;
        e->x = e->sx; e->y = e->sy;
        if (e->kind == ENT_CHICKEN || e->kind == ENT_RINO) {
            e->vx = 0.0f;  // facing kept from Tiled spawn; update loop drives movement
        } else {
            e->vx = (def->speed > 0.0f) ? (((i & 1) == 0) ? -def->speed : def->speed) : 0.0f;
            e->facing = e->vx >= 0.0f ? 1 : -1; // Facing towards right if movement more than 0
        }
        e->vy = 0.0f;
        e->grounded = false;
        e->activated = false;
        e->animT = 0.0f;
        e->atkT = 0.0f; e->coolT = 0.0f; e->attacking = false; e->fired = false;
        e->animState = (e->kind == ENT_BAT) ? 0 : 1; e->stateT = 0.0f; e->deathT = 0.0f;  // bat back to perch, rest to run, clear Hit
    }

    if (respawnSnd) CP_Sound_Play(respawnSnd); // play sound whenever player respawn
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

 
//  extract.c (map render lives with map data)
// load_map_json, render_map_json, process_layers_world, parse_object_group_entities

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

// Pause menu (pause_row_y, draw_pause_menu, handle_pause_input) now owned by
// pause.c — the game loop below calls draw/handle via pause.h.

void game_update(void) {
    // Keep the logical screen in sync with the fullscreen (or resized) window
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // Game state only now (menu lives in menu.c; the engine switches states,
    // so the old STATE_MENU / STATE_GAME switch is gone).
    {
        float dt = CP_System_GetDt();
        if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;  // clamp hitches, prevents tunneling

        // ESC ("exit" key): cancel confirm popup, open pause menu, step back a page, or resume
        if (CP_Input_KeyTriggered(KEY_ESCAPE)) {
            if (pause_confirm_open()) pause_cancel_confirm();  // ESC closes popup, stays paused
            else if (!paused) {
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
        render_map_json();      // navy tiles cover the rest (extract.c)
        render_entities();      // checkpoints, enemies, traps from map.json (extract.c)
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
            CP_Font_DrawText("LEVEL COMPLETE", SCREEN_WIDTH * 0.5f, 400.0f);
            CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
            CP_Settings_TextSize(30.0f);
            CP_Font_DrawText("Click anywhere to return to menu", SCREEN_WIDTH * 0.5f, 500.0f);
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
    // switch (menu can be revisited) so it frees nothing — menu button art
    // (extern from menu.h) is freed inline here, once, at terminate.
    // NOTE: game_exit ALSO runs on every quit-to-menu (engine exits the old
    // state on switch), so every pointer freed here is set back to NULL and
    // entityCount to 0. Otherwise menu_init/game_init reload guards see stale
    // non-NULL handles, skip reloading, and the menu draws freed memory
    // (that use-after-free was the finish-game crash).
    if (playBtnImage) CP_Image_Free(playBtnImage);
    if (volBtnImage) CP_Image_Free(volBtnImage);
    playBtnImage = NULL; volBtnImage = NULL;
    if (creditBtnImage) CP_Image_Free(creditBtnImage);
    if (digipenLogoImage) CP_Image_Free(digipenLogoImage);
    creditBtnImage = NULL; digipenLogoImage = NULL;
    if (buildlabLogoImage) CP_Image_Free(buildlabLogoImage);
    buildlabLogoImage = NULL;
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