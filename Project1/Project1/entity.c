#include <math.h>
#include "cprocessing.h"
#include "utils.h"   // TILE_SIZE, SCREEN_WIDTH, is_solid_px, aabb_overlap, dust/pickup pools, sounds, levelComplete
#include "player.h"  // player state, hurt_player (the player half of the seam)
#include "game.h"    // camX/camY
#include "extract.h" // Entity/EntityDef/ENTITY_DEFS/entityPool (spawn fills them, this reads them)
#include "entity.h"  // entity update/render/collision entry points
#include "quiz.h"    // checkpoint learning topics + End quiz trigger

#define ENT_GRAVITY 2400.0f
#define ENT_MAX_FALL 950.0f

// Move every living object (gravity + chicken/rino patrol; traps stay put)
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

        // Enemies Hit flash after hurting the PLAYER (stomp-deaths use deathT instead).
        if (e->animState == 2 && e->stateT > 0.5f) { e->animState = 1; e->stateT = 0.0f; }

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
        // walkers are handled here.
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
        // Multi-state enemies (Chicken/Rino Hit): pick the current strip.
        bool hasStates = e->idleImg || e->runImg || e->hitImg;
        if (hasStates) {
            if (e->animState == 2 && e->hitImg) img = e->hitImg;
            // else if (e->animState == 0 && e->idleImg) img = e->idleImg; // no idle strip for any entity
            else if (e->animState == 1 && e->runImg) img = e->runImg;
            else if (e->baseImage) img = e->baseImage;
        }
        if (!img) continue;

        int frames = e->totalFrames;
        if (hasStates && img != e->baseImage) {
            int iw2 = CP_Image_GetWidth(img);
            frames = (iw2 > e->cellW) ? iw2 / e->cellW : 1;
        }
        if (e->kind == ENT_CHECKPOINT)
            frames = e->activated ? (CP_Image_GetWidth(img) / e->cellW) : 1;
        if (frames < 1) frames = 1;

        float animFps = (e->kind == ENT_CHECKPOINT && e->activated) ? 10.0f : def->animFps;
        int frame = (frames > 1) ? (((int)(e->animT * animFps)) % frames) : 0;
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
        // Chicken/Rino art is taller than its hitbox, so center-drawing sank
        // their legs into tiles (see animal.png). Anchor these at the feet instead:
        // sprite bottom = hitbox bottom, which physics keeps on the tile top.
        // (Checkpoints/Spikes were fixed the other way: lifted at spawn + centered draw.)
        if (e->kind == ENT_CHICKEN || e->kind == ENT_RINO)
            sy = roundf(e->y + e->h * 0.5f - dh - camY);
        if (sx < -192.0f || sx > SCREEN_WIDTH + 192.0f ||
            sy < -192.0f || sy > SCREEN_HEIGHT + 192.0f) continue;

        // Rino/Chicken strips are authored facing left: mirror them so they
        // face the direction they move in
        int dfacing = e->facing;
        if (e->kind == ENT_RINO || e->kind == ENT_CHICKEN) dfacing = -dfacing;
        draw_sheet_frame(img, e->cellW, e->cellH, frame, sx, sy, dw, dh, dfacing, 255);
    }
}

// Player vs map objects: engage checkpoints, stomp enemies, take damage.
// (This is the player<->enemy seam: reads player state, calls hurt_player,
// mutates entities. Fruits + checkpoints ride the same loop.)
// Entity Structure is defined in extract.h
void check_player_entity_collisions(void) {
    // player position is already the top-left corner
    // player left = player.x, player top = player.y
    float px = player.x, py = player.y, pw = PLAYER_HB_W, ph = PLAYER_HB_H;

    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        if (!e->active) continue;      // skips inactive objects
        if (e->deathT > 0.0f) continue;  // skips rino or chicken that has died 

        const EntityDef* def = &ENTITY_DEFS[e->kind];
        // Entity: x, y  = center position. Then, w, h = hitbox size of (chicken,rino, fruit, spike)
        // left (hx) = center X - half width
        // top (hy) = center Y - half height
        float hx = e->x - e->w * 0.5f, hy = e->y - e->h * 0.5f;

        // Checks if player reaches Checkpoint / End gates:
        if (e->kind == ENT_CHECKPOINT || e->kind == ENT_END) {
            // checkpoint/EndPoint entity hitbox rectangle compares with player hitbox rectangle
            if (aabb_overlap(px, py, pw, ph, hx, hy, e->w, e->h)) { // defined in utils.c
                if (e->kind == ENT_END) {
                    levelComplete = true;    // reach the End gate: level done
                    record_highscore();     // seed/update highscore.txt if this run is the best
                } else if (!e->activated) { // e->activated == false
                    e->activated = true;    // activate checkpoint
                    // set player new Respawn location:
                    respawnX = e->x;        
                    respawnY = e->y;
                    checkpointScore = score;   // respawn here keeps only the points banked at touch
                    if (coinSnd) CP_Sound_Play(coinSnd);
                    quiz_start_checkpoint_quiz();   // first touch: 1 quiz -> buff if correct
                }
            }
            continue; // skip loop if player did not interact with checkpoint or endPoint
        }

        if (e->kind == ENT_START) continue;  // skip start checkPoint entity to move to next entity.

        // Fruits: collect on touch. Airborne grabs stack the combo (up to 8x);
        // the popup shows the run total so fruits are worth chaining for.
        if (e->kind >= ENT_FRUIT_ORANGE && e->kind <= ENT_FRUIT_STRAWBERRY) {
            if (aabb_overlap(px, py, pw, ph, hx, hy, e->w, e->h)) { // utils.c
                e->active = false; // once interact with fruits, make fruits disappear 
                // combo occurs if player is in air and while combo (1) < 8 (max).
                if (!player.grounded && combo < COMBO_MAX) combo++; // combo defined in utils.c
                int gained = 50 * combo;
                score += gained;
                // spawn point number "gained" above fruit:
                spawn_popup(e->x, e->y - 14.0f, gained); // print score above fruits
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
