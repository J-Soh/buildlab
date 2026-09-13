// entity.c
// Entity pool definitions, static definition table, entity AI & movement,
// collision detection with player, projectile system, and entity drawing.
#include "entity.h"
#include "map.h"
#include "player.h"
#include "audio.h"
#include "cJSON.h"

// ---- Entity pool ----
Entity entityPool[MAX_ENTITIES];
int    entityCount = 0;

// ---- Static definition table ----
const EntityDef ENTITY_DEFS[ENT_COUNT] = {
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

// ---- Plant projectile art ----
CP_Image plantAtkImg = NULL;
CP_Image bulletImg = NULL;
CP_Image piecesImg = NULL;

// ---- Bullet pool ----
Bullet   bullets[MAX_BULLETS];
BulletFX bulletFx[MAX_BULLET_FX];

// ---- Pickup sparkle pool ----
PickupFX pickupFx[MAX_PICKUP_FX];
CP_Image collectedImg = NULL;

void spawn_pickup_fx(float wx, float wy) {
    for (int i = 0; i < MAX_PICKUP_FX; i++) {
        if (!pickupFx[i].active) {
            pickupFx[i].active = true;
            pickupFx[i].x = wx; pickupFx[i].y = wy; pickupFx[i].t = 0.0f;
            return;
        }
    }
}

// Map the Tiled object layer name to an EntityKind
EntityKind kind_from_layer_name(const char* name) {
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

Entity* spawn_entity(EntityKind kind, float wx, float wy) {
    if (entityCount >= MAX_ENTITIES || kind >= ENT_COUNT) return NULL;

    const EntityDef* def = &ENTITY_DEFS[kind];
    Entity* e = &entityPool[entityCount++];

    e->kind = kind;
    e->active = true;
    e->baseImage = def->path ? CP_Image_Load(def->path) : NULL;
    e->altImage = def->altPath ? CP_Image_Load(def->altPath) : NULL;
    e->idleImg = def->idlePath ? CP_Image_Load(def->idlePath) : NULL;
    e->runImg = def->runPath ? CP_Image_Load(def->runPath) : NULL;
    e->hitImg = def->hitPath ? CP_Image_Load(def->hitPath) : NULL;
    if (kind == ENT_BAT) {
        e->emergeImg = CP_Image_Load("Assets/Enemies/Bat/Ceiling In (46x30).png");
    } else {
        e->emergeImg = NULL;
    }
    e->animState = 1; e->stateT = 0.0f; e->deathT = 0.0f;
    e->cellW = def->cellW;
    e->cellH = def->cellH;
    e->totalFrames = 1;

    if (e->baseImage) {
        int iw = CP_Image_GetWidth(e->baseImage);
        if (iw > def->cellW) e->totalFrames = iw / def->cellW;
    }

    e->x = wx; e->y = wy;
    e->sx = wx; e->sy = wy;

    e->w = (float)def->cellW * entityScale * def->hbW;
    e->h = (float)def->cellH * entityScale * def->hbH;

    if (kind == ENT_START || kind == ENT_CHECKPOINT || kind == ENT_END || kind == ENT_SPIKES) {
        e->y -= (float)def->cellH * entityScale * 0.5f;
        e->sy = e->y;
    }

    if (mapData && (kind == ENT_CHICKEN || kind == ENT_RINO || kind == ENT_PLANT)) {
        float halfH2 = e->h * 0.5f;
        float bottom = e->y + halfH2;
        if (is_solid_px(e->x, bottom)) {
            e->y = ((int)(bottom / TILE_SIZE)) * TILE_SIZE - halfH2 - PHYS_SNAP_EPS;
        } else {
            for (float d = ENT_GROUND_STEP; d <= ENT_GROUND_SCAN; d += ENT_GROUND_STEP) {
                if (is_solid_px(e->x, bottom + d)) {
                    e->y = ((int)((bottom + d) / TILE_SIZE)) * TILE_SIZE - halfH2 - PHYS_SNAP_EPS;
                    break;
                }
            }
        }
        e->sy = e->y;
        e->vy = 0.0f;
        e->grounded = true;
    }

    if (kind == ENT_BAT && mapData) {
        float surface = 0.0f;
        for (float d = 0.0f; d <= BAT_CEILING_SCAN; d += BAT_PROBE_STEP) {
            float py = wy - d;
            if (is_solid_px(wx, py)) { surface = ((int)(py / TILE_SIZE) + 1) * TILE_SIZE; break; }
        }
        if (surface > 0.0f) {
            e->x = wx; e->y = surface + e->h * BAT_CEILING_TUCK;
        } else {
            e->x = wx; e->y = wy;
        }
        e->sx = e->x; e->sy = e->y;
        e->animState = 0;
    }

    e->vx = (def->speed > 0.0f) ? (((entityCount & 1) == 0) ? -def->speed : def->speed) : 0.0f;
    e->vy = 0.0f;
    e->facing = e->vx >= 0.0f ? 1 : -1;
    e->animT = 0.0f;
    if (kind != ENT_CHICKEN && kind != ENT_RINO && kind != ENT_PLANT) e->grounded = false;
    e->activated = false;
    return e;
}

void update_bullets(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet* b = &bullets[i];
        if (!b->active) continue;
        b->life -= dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        bool dead = (b->life <= 0.0f);
        if (!dead && is_solid_px(b->x, b->y)) dead = true;
        if (!dead && b->x > player.x - BULLET_HIT_PADDING && b->x < player.x + PLAYER_HB_W + BULLET_HIT_PADDING &&
            b->y > player.y - BULLET_HIT_PADDING && b->y < player.y + PLAYER_HB_H + BULLET_HIT_PADDING) {
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
            if (bulletFx[f].t >= BULLET_FX_TIMEOUT) bulletFx[f].active = false;
        }
    }
}

void draw_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active || !bulletImg) continue;
        float sx = roundf(bullets[i].x - 20.0f - camX);
        float sy = roundf(bullets[i].y - 20.0f - camY);
        if (sx < -BULLET_CULL_MARGIN || sx > screenW + BULLET_CULL_MARGIN ||
            sy < -BULLET_CULL_MARGIN || sy > screenH + BULLET_CULL_MARGIN) continue;
        CP_Image_Draw(bulletImg, sx, sy, 40.0f, 40.0f, 255);
    }
    for (int f = 0; f < MAX_BULLET_FX; f++) {
        if (!bulletFx[f].active || !piecesImg) continue;
        int fr = (bulletFx[f].t > BULLET_FX_MID) ? 1 : 0;
        draw_sheet_frame(piecesImg, 16, 16, fr, roundf(bulletFx[f].x - 20.0f - camX), roundf(bulletFx[f].y - 20.0f - camY), 40.0f, 40.0f, 1, 255);
    }
}

void update_entities(float dt) {
    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        e->animT += dt;
        e->stateT += dt;

        if (e->deathT > 0.0f) {
            e->deathT -= dt;
            if (e->deathT <= 0.0f) e->active = false;
            continue;
        }

        if (e->animState == 2 && e->stateT > ENT_HIT_FLASH_DUR) { e->animState = 1; e->stateT = 0.0f; }

        if (def->flies) {
            float pcx = player.x + PLAYER_HB_W * 0.5f;
            float pcy = player.y + PLAYER_HB_H * 0.5f;
            float dx = pcx - e->x, dy = pcy - e->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (e->animState == 0) {  // perched
                e->vx = 0.0f; e->vy = 0.0f;
                if (dist < BAT_AGGRO_RADIUS && !levelComplete) { e->animState = 3; e->stateT = 0.0f; }
            } else if (e->animState == 3) {  // Ceiling In alert burst
                if (e->stateT > BAT_EMERGE_TIME) { e->animState = 1; e->stateT = 0.0f; }
            } else if (e->animState == 1) {  // flying
                float tx, ty;
                if (dist < BAT_AGGRO_RADIUS * BAT_AGGRO_CHASE_MULT && !levelComplete) { tx = pcx; ty = pcy; }
                else { tx = e->sx; ty = e->sy; }
                float mx = tx - e->x, my = ty - e->y;
                float ml = sqrtf(mx * mx + my * my);
                if (ml > 1.0f) {
                    float nx = e->x + mx / ml * BAT_CHASE_SPEED * dt;
                    float ny = e->y + my / ml * BAT_CHASE_SPEED * dt;
                    if (!is_solid_px(nx + ((mx > 0.0f) ? e->w * 0.5f : -e->w * 0.5f), e->y)) e->x = nx;
                    if (!is_solid_px(e->x, ny + ((my > 0.0f) ? e->h * 0.5f : -e->h * 0.5f))) e->y = ny;
                    e->facing = (mx >= 0.0f) ? 1 : -1;
                }
                float hx = e->sx - e->x, hy = e->sy - e->y;
                if (dist >= BAT_AGGRO_RADIUS * BAT_AGGRO_CHASE_MULT && sqrtf(hx * hx + hy * hy) < BAT_HOME_SNAP_DIST) {
                    e->x = e->sx; e->y = e->sy;
                    e->animState = 0; e->stateT = 0.0f;
                }
            }
            continue;
        }

        if (e->kind == ENT_PLANT) {
            float pcx = player.x + PLAYER_HB_W * 0.5f;
            float pcy = player.y + PLAYER_HB_H * 0.5f;
            float dx = pcx - e->x, dy = pcy - e->y;
            e->facing = (dx >= 0.0f) ? 1 : -1;
            e->coolT -= dt;
            if (e->attacking) {
                e->atkT += dt;
                if (!e->fired && e->atkT >= PLANT_FIRE_TIME) {
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
                                bullets[b].life = BULLET_LIFE;
                                break;
                            }
                        }
                    }
                }
                if (e->atkT >= PLANT_ATK_END) { e->attacking = false; e->coolT = PLANT_COOLDOWN; }
            }
            else if (e->coolT <= 0.0f && !levelComplete &&
                     fabsf(dx) < PLANT_AGGRO_X && fabsf(dy) < PLANT_AGGRO_Y) {
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

        e->vy += ENT_GRAVITY * dt;
        if (e->vy > ENT_MAX_FALL) e->vy = ENT_MAX_FALL;
        e->y += e->vy * dt;
        float halfH = e->h * 0.5f;
        float bottom = e->y + halfH;
        if (is_solid_px(e->x - e->w * 0.35f, bottom) || is_solid_px(e->x + e->w * 0.35f, bottom)) {
            e->y = ((int)(bottom / TILE_SIZE)) * TILE_SIZE - halfH - PHYS_SNAP_EPS;
            e->vy = 0.0f;
            e->grounded = true;
        } else {
            e->grounded = false;
        }

        if ((e->kind == ENT_CHICKEN || e->kind == ENT_RINO) && e->grounded) {
            float pcx = player.x + PLAYER_HB_W * 0.5f;
            float pcy = player.y + PLAYER_HB_H * 0.5f;
            float dx = pcx - e->x, dy = pcy - e->y;
            bool seen = !levelComplete && ((dx * (float)e->facing) > 0.0f)
                     && fabsf(dx) < WALKER_AGGRO_X && fabsf(dy) < WALKER_AGGRO_Y;
            if (e->animState == 2) {
                e->vx = 0.0f;
            } else {
                e->animState = 1;
                if (seen) e->facing = (dx >= 0.0f) ? 1 : -1;
                e->vx = (float)e->facing * def->speed * (seen ? WALKER_CHARGE_MULT : 1.0f);
                float nx = e->x + e->vx * dt;
                float fx = (e->vx > 0.0f) ? nx + e->w * 0.5f : nx - e->w * 0.5f;
                bool wall = is_solid_px(fx, e->y - halfH * 0.6f) || is_solid_px(fx, e->y + 2.0f);
                float ahead = nx + ((e->vx > 0.0f) ? (e->w * 0.5f + ENT_LEDGE_AHEAD) : -(e->w * 0.5f + ENT_LEDGE_AHEAD));
                bool ledge = !is_solid_px(ahead, e->y + halfH + ENT_LEDGE_PROBE);
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

void check_player_entity_collisions(void) {
    float px = player.x, py = player.y, pw = PLAYER_HB_W, ph = PLAYER_HB_H;

    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;
        if (e->deathT > 0.0f) continue;

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        float hx = e->x - e->w * 0.5f, hy = e->y - e->h * 0.5f;

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

        if (e->kind == ENT_START) continue;

        if (e->kind >= ENT_FRUIT_ORANGE && e->kind <= ENT_FRUIT_STRAWBERRY) {
            if (aabb_overlap(px, py, pw, ph, hx, hy, e->w, e->h)) {
                e->active = false;
                spawn_pickup_fx(e->x, e->y);
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

        float playerBottom = py + ph;
        float entTop = e->y - e->h * 0.5f;
        if (player.vy > STOMP_MIN_VY && playerBottom <= entTop + STOMP_LENIENCY) {
            if (e->hitImg) {
                e->animState = 2; e->stateT = 0.0f; e->deathT = 0.45f;
                e->vx = 0.0f; e->vy = 0.0f;
                e->attacking = false; e->fired = false;
            } else {
                e->active = false;
            }
            if (playerHitSnd) CP_Sound_Play(playerHitSnd);
            for (int k = 0; k < STOMP_DUST_COUNT; k++)
                spawn_dust(e->x + (float)((k % 3) - 1) * 14.0f, e->y - e->h * 0.2f,
                           (float)((k % 3) - 1) * 90.0f, STOMP_DUST_VY, STOMP_DUST_LIFE, STOMP_DUST_SIZE);
            player.vy = STOMP_BOUNCE_VY;
            player.jumps = 1;
            player.doubleAnim = false;
        } else {
            if (e->hitImg) { e->animState = 2; e->stateT = 0.0f; }
            hurt_player();
        }
    }
}

void render_entities(void) {
    CP_Settings_ImageMode(CP_POSITION_CORNER);

    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        CP_Image img = e->baseImage;
        if (e->kind == ENT_CHECKPOINT && e->activated && e->altImage) img = e->altImage;
        bool plantFiring = (e->kind == ENT_PLANT && e->attacking && e->deathT <= 0.0f && plantAtkImg);
        if (plantFiring) img = plantAtkImg;
        bool hasStates = !plantFiring && (e->idleImg || e->runImg || e->hitImg);
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
        if (plantFiring) frames = 8;
        if (frames < 1) frames = 1;

        float animFps = (e->kind == ENT_CHECKPOINT && e->activated) ? 10.0f : def->animFps;
        if (plantFiring) animFps = 12.0f;
        int frame = (frames > 1) ? (((int)(e->animT * animFps)) % frames) : 0;
        if (plantFiring) { frame = (int)(e->atkT * 12.0f); if (frame > 7) frame = 7; }
        if (hasStates && e->animState == 2) {
            frame = (int)(e->stateT * 10.0f);
            if (frame > frames - 1) frame = frames - 1;
            if (frame < 0) frame = 0;
        }

        bool isFruit = e->kind >= ENT_FRUIT_ORANGE && e->kind <= ENT_FRUIT_STRAWBERRY;
        float dw = isFruit ? TILE_SIZE : (float)e->cellW * entityScale;
        float dh = isFruit ? TILE_SIZE : (float)e->cellH * entityScale;
        float sx = roundf(e->x - dw * 0.5f - camX);
        float sy = roundf(e->y - dh * 0.5f - camY);
        if (e->kind == ENT_CHICKEN || e->kind == ENT_RINO || e->kind == ENT_PLANT)
            sy = roundf(e->y + e->h * 0.5f - dh - camY);
        if (sx < -ENT_CULL_MARGIN || sx > screenW + ENT_CULL_MARGIN ||
            sy < -ENT_CULL_MARGIN || sy > screenH + ENT_CULL_MARGIN) continue;

        int dfacing = e->facing;
        if (e->kind == ENT_RINO || e->kind == ENT_CHICKEN || e->kind == ENT_PLANT) dfacing = -dfacing;
        draw_sheet_frame(img, e->cellW, e->cellH, frame, sx, sy, dw, dh, dfacing, 255);
    }
}

static void parse_object_group_entities(cJSON* layer) {
    cJSON* nameItem = cJSON_GetObjectItemCaseSensitive(layer, "name");
    const char* name = (cJSON_IsString(nameItem) && nameItem->valuestring) ? nameItem->valuestring : "";

    EntityKind kind = kind_from_layer_name(name);
    if (kind >= ENT_COUNT) return;

    cJSON* objects = cJSON_GetObjectItemCaseSensitive(layer, "objects");
    if (!cJSON_IsArray(objects)) return;

    int n = cJSON_GetArraySize(objects);

    for (int i = 0; i < n; i++) {
        cJSON* obj = cJSON_GetArrayItem(objects, i);
        if (!obj) continue;

        cJSON* x = cJSON_GetObjectItemCaseSensitive(obj, "x");
        cJSON* y = cJSON_GetObjectItemCaseSensitive(obj, "y");

        if (cJSON_IsNumber(x) && cJSON_IsNumber(y)) {
            Entity* se = spawn_entity(kind, (float)x->valuedouble * entityScale, (float)y->valuedouble * entityScale);
            if (se && (kind == ENT_CHICKEN || kind == ENT_RINO)) {
                cJSON* oname = cJSON_GetObjectItemCaseSensitive(obj, "name");
                const char* on = (cJSON_IsString(oname) && oname->valuestring) ? oname->valuestring : "";
                if (strstr(on, "left")) se->facing = -1;
                else if (strstr(on, "right")) se->facing = 1;
                se->vx = 0.0f;
            }
        }
    }
}

void process_layers_world_entities(void* layersJson) {
    parse_object_group_entities((cJSON*)layersJson);
}
