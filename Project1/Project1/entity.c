#include <math.h>
#include "cprocessing.h"
#include "utils.h"   // TILE_SIZE, SCREEN_WIDTH, is_solid_px, aabb_overlap, dust/pickup pools, sounds, levelComplete
#include "player.h"  // player state, hurt_player (the player half of the seam)
#include "game.h"    // camX/camY + plant/bullet art (game-owned render state)
#include "extract.h" // Entity/EntityDef/ENTITY_DEFS/entityPool (spawn fills them, this reads them)
#include "entity.h"  // own bullet pools, update/render/collision entry points
#include "quiz.h"    // checkpoint learning topics + End quiz trigger

Bullet bullets[MAX_BULLETS];
BulletFX bulletFx[MAX_BULLET_FX];
PlayerBullet playerBullets[MAX_PLAYER_BULLETS];

#define ENT_GRAVITY 2400.0f
#define ENT_MAX_FALL 950.0f

// Bat sleeper tuning (world px; 48px = 1 tile). BAT_AGGRO_RADIUS is the chase
// trigger around the bat — raise for earlier ambushes, lower for later ones.
#define BAT_AGGRO_RADIUS 344.0f  // Wake up bat if player is within 8 tiles.
#define BAT_CHASE_SPEED 150.0f   // world px/s toward the player / back to perch
#define BAT_EMERGE_TIME 0.45f    // Ceiling In alert length before the chase

// Seeds fly straight, pop into pieces on tiles, hurt on touch (iframe-gated)
void update_bullets(float dt) {
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
        if (!bulletFx[f].active) continue;
        bulletFx[f].t += dt;
        if (bulletFx[f].t >= 0.3f) bulletFx[f].active = false;
    }
}

void draw_bullets(void) {
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

// Move every living object (gravity + patrol; bats hover; traps stay put)
void update_entities(float dt) {
    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        e->animT += dt;
        e->stateT += dt;

        // Enemies Dying (stomped): freeze, play one-shot Hit strip in draw, then deactivate
        if (e->deathT > 0.0f) {
            e->deathT -= dt;
            if (e->deathT <= 0.0f) e->active = false;
            continue;
        }

        // Enemies Hit flash after hurting the PLAYER (stomp-deaths use deathT instead):
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
                // (enemySpeedMul: Fortitude buff makes everything 20% faster)
                e->vx = (float)e->facing * def->speed * enemySpeedMul * (seen ? 2.5f : 1.0f);
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

// Draw all extracted map objects at their world positions (camera-relative)
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

// Player vs map objects: engage checkpoints, stomp enemies, take damage.
// (This is the player<->enemy seam: reads player state, calls hurt_player,
// mutates entities. Fruits + checkpoints ride the same loop.)
void check_player_entity_collisions(void) {
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
                    levelComplete = true;    // reach the End gate: level done (no quiz)
                } else if (!e->activated) {
                    e->activated = true;
                    respawnX = e->x;
                    respawnY = e->y;
                    if (coinSnd) CP_Sound_Play(coinSnd);
                    quiz_start_checkpoint_quiz();   // first touch: 1 quiz -> buff if correct
                }
            }
            continue;
        }

        if (e->kind == ENT_START) continue;  // spawn marker only

        // Fruits: collect on touch. Airborne grabs stack the combo (up to 8x);
        // the popup shows the run total so fruits are worth chaining for.
        if (e->kind >= ENT_FRUIT_ORANGE && e->kind <= ENT_FRUIT_STRAWBERRY) {
            if (aabb_overlap(px, py, pw, ph, hx, hy, e->w, e->h)) {
                e->active = false;
                if (!player.grounded && combo < COMBO_MAX) combo++;
                int gained = 50 * combo;
                score += gained;
                spawn_popup(e->x, e->y - 14.0f, gained);
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
            score += 100;
            spawn_popup(e->x, e->y - e->h * 0.5f, 100);
            hitStopT = 0.06f;   // freeze a beat so the stomp lands
            shakeT = 0.18f;     // camera kick
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
