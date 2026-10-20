#include <math.h>
#include "cprocessing.h"
#include "utils.h"   // TILE_SIZE, is_solid_px, dust pool, sounds, hearts/levelComplete, draw_sprite_frame
#include "game.h"    // jumpSnd (jump SFX), camX/camY + player art (render_player)
#include "player.h"  // own Player state, tuning, entry points
#include "entity.h"  // playerBullets pool (fired with E)

Player player;

// Axis-separated AABB move + collide against solid tiles
// doubleAnimT = double-jump animation clock. 
// doubleAnim latches true when the double jump fires; render_player() uses doubleAnimT x 12 for the flip frame (held at frame 5). 
// It unlatches (false) on landing or stomp-bounce.
void player_physics(float dt) {
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

// Deal damage to the player (iframe-gated) from an enemy/trap contact
// player gets knockback in opposite direction
// invulnT = invulnerability timer (1.4 seconds after a hit, 1.2 after death). 
// While above 0.0f, hurt_player() and enemy contact do nothing, and the player sprite flashes (alternates 130/255 alpha).
void hurt_player(void) {
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

// Per-frame player update: input + jump/double + gravity + timers, then
// physics, then footsteps. Same order as the old inline block in game_update.
void player_update(float dt) {
    // --- Input: A/D move, SPACE jump/double (single move speed) ---
    bool left = CP_Input_KeyDown(KEY_A);
    bool right = CP_Input_KeyDown(KEY_D);
    float moveSpeed = MOVE_SPEED * moveSpeedMul;  // Swift Feet buff = 1.2x
    if (player.lockT > 0.0f) player.vx = player.kbx;
    else if (left && !right) { player.vx = -moveSpeed; player.facing = -1; }
    else if (right && !left) { player.vx = moveSpeed; player.facing = 1; }
    else player.vx = 0.0f;

    // Punctual shot on E: spends points, levitates the "pew" (jump SFX, high pitch)
    if (CP_Input_KeyTriggered(KEY_E)) {
        if (score >= bulletCost) {
            for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
                if (!playerBullets[i].active) {
                    playerBullets[i].active = true;
                    playerBullets[i].x = player.x + PLAYER_HB_W * 0.5f + (float)player.facing * 34.0f;
                    playerBullets[i].y = player.y + PLAYER_HB_H * 0.42f;
                    playerBullets[i].vx = 900.0f * (float)player.facing;
                    playerBullets[i].life = 1.0f;
                    break;
                }
            }
            score -= bulletCost;
            spawn_popup(player.x + PLAYER_HB_W * 0.5f, player.y, -bulletCost);
            if (jumpSnd) CP_Sound_PlayAdvanced(jumpSnd, 0.6f, 1.7f, (CP_BOOL)0, CP_SOUND_GROUP_SFX);
        } else if (hitSnd) {
            CP_Sound_Play(hitSnd);  // not enough points: dry click
        }
    }

    if (CP_Input_KeyTriggered(KEY_SPACE)) player.buffer = JUMP_BUFFER;

    // --- Jump / double jump ---
    if (player.buffer > 0.0f) {
        if (player.grounded || player.coyote > 0.0f) {
            player.vy = JUMP_VEL; // -980
            player.jumps = 1;
            player.grounded = false;
            player.coyote = 0.0f; player.buffer = 0.0f;
            if (jumpSnd) CP_Sound_PlayAdvanced(jumpSnd, 0.9f, 1.0f, (CP_BOOL)0, CP_SOUND_GROUP_SFX);
            spawn_dust(player.x + PLAYER_HB_W * 0.5f, player.y + PLAYER_HB_H - 4.0f, 0.0f, -40.0f, 0.35f, 22.0f); // utils.c
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
    player.vy += GRAVITY * dt; // 2800 * dt
    if (player.vy > MAX_FALL) player.vy = MAX_FALL; // 1150: prevent player.vy from going big numbers during the fall.

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
}

// Player render: ground shadow blob + animation select + draw
void render_player(void) {
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
}
