// player.c
// Player character globals, physics implementation, dust system, and rendering helpers.
#include "player.h"
#include "map.h"
#include "entity.h"
#include "audio.h"

// ---- Player instance ----
Player player;
bool   runStarted = false;   // true after first menu->game transition
bool   levelComplete = false;
float  respawnX = -1.0f, respawnY = -1.0f;  // player CENTER, world px
float  startX = -1.0f, startY = -1.0f;      // player CENTER, world px

// ---- Player visual assets ----
CP_Image playerIdle = NULL;    // 11 frames
CP_Image playerRun = NULL;     // 12 frames
CP_Image playerJump = NULL;    // 1 frame
CP_Image playerDouble = NULL;  // 6 frames
CP_Image playerFall = NULL;    // 1 frame
CP_Image shadowImg = NULL;
CP_Image dustImg = NULL;

// ---- Hearts HUD asset & state ----
CP_Image heartImg = NULL;  // 16x16 cells: 0 full, 1 empty
int      hearts = MAX_HEARTS;

// ---- Dust particle pool ----
Dust dustPool[MAX_DUST];

// Standard AABB overlap test
bool aabb_overlap(float ax, float ay, float aw, float ah,
                 float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// Fresh start from the beginning: forget checkpoints, then reset.
void restart_from_beginning(void) {
    respawnX = -1.0f; respawnY = -1.0f;
    reset_run();
}

void reset_run(void) {
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
    camX = player.x + PLAYER_HB_W * 0.5f - screenW * 0.5f;
    camY = player.y + PLAYER_HB_H * 0.5f - screenH * 0.5f;
}

// Axis-separated AABB move + collide against solid tiles
void player_physics(float dt) {
    // --- X axis ---
    player.x += player.vx * dt;
    if (player.vx > 0.0f) {
        float ex = player.x + PLAYER_HB_W;
        if (is_solid_px(ex, player.y + PHYS_PROBE_INSET) ||
            is_solid_px(ex, player.y + PLAYER_HB_H * 0.5f) ||
            is_solid_px(ex, player.y + PLAYER_HB_H - PHYS_PROBE_INSET)) {
            player.x = ((int)(ex / TILE_SIZE)) * TILE_SIZE - PLAYER_HB_W - PHYS_SNAP_EPS;
            player.vx = 0.0f;
        }
    }
    else if (player.vx < 0.0f) {
        if (is_solid_px(player.x, player.y + PHYS_PROBE_INSET) ||
            is_solid_px(player.x, player.y + PLAYER_HB_H * 0.5f) ||
            is_solid_px(player.x, player.y + PLAYER_HB_H - PHYS_PROBE_INSET)) {
            player.x = (((int)floorf(player.x / TILE_SIZE)) + 1) * TILE_SIZE + PHYS_SNAP_EPS;
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
        bool hit = is_solid_px(player.x + PHYS_PROBE_INSET, ey) || is_solid_px(player.x + PLAYER_HB_W - PHYS_PROBE_INSET, ey);
        if (hit) {
            player.y = ((int)(ey / TILE_SIZE)) * TILE_SIZE - PLAYER_HB_H - PHYS_SNAP_EPS;
            player.vy = 0.0f;
            player.grounded = true;
            player.jumps = 0;
            player.doubleAnim = false;
        }
    }
    else {
        if (is_solid_px(player.x + PHYS_PROBE_INSET, player.y) || is_solid_px(player.x + PLAYER_HB_W - PHYS_PROBE_INSET, player.y)) {
            player.y = (((int)floorf(player.y / TILE_SIZE)) + 1) * TILE_SIZE + PHYS_SNAP_EPS;
            player.vy = 0.0f;
        }
    }
    if (player.grounded && !wasGrounded && fallSpeed > LANDING_SND_VY && stepSnd) {
        CP_Sound_Play(stepSnd);  // landing thud reuses footstep
        for (int k = 0; k < LANDING_DUST_COUNT; k++) {
            for (int i = 0; i < MAX_DUST; i++) {
                if (!dustPool[i].active) {
                    dustPool[i].active = true;
                    dustPool[i].x = player.x + PLAYER_HB_W * 0.5f + (float)((k % 3) - 1) * DUST_LAND_SPREAD;
                    dustPool[i].y = player.y + PLAYER_HB_H - 4.0f;
                    dustPool[i].vx = (float)((k % 3) - 1) * 90.0f;
                    dustPool[i].vy = -60.0f - (float)(k / 3) * 60.0f;
                    dustPool[i].life = dustPool[i].maxLife = DUST_LAND_LIFE;
                    dustPool[i].size = DUST_LAND_SIZE;
                    break;
                }
            }
        }
    }
}

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

// Draw one frame of an arbitrary cell strip; facing -1 mirrors via swapped U
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

// Deal damage to the player (iframe-gated) from an enemy/trap contact
void hurt_player(void) {
    if (player.invulnT > 0.0f || levelComplete) return;

    // Traps and enemies consume ONE heart (never an instant kill)
    hearts--;
    if (hitSnd) CP_Sound_Play(hitSnd);   // player got hit
    player.invulnT = HIT_INVULN_TIME;
    player.lockT = HIT_LOCK_TIME;
    player.kbx = -HIT_KNOCKBACK_VX * (float)player.facing;
    if (player.vy > HIT_KB_VY_THRESHOLD) player.vy = HIT_KNOCKBACK_VY;  // hop up + knock back

    if (hearts <= 0) {
        hearts = 0;
        if (deathSnd) CP_Sound_Play(deathSnd);
        reset_run();
        player.invulnT = DEATH_INVULN_TIME;
    }
}
