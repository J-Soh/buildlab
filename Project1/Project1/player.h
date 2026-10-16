#pragma once
// Player mechanics: state, input, physics, damage, render.
// (The player<->enemy seam lives in exactly two calls: hurt_player() here and
// check_player_entity_collisions() in entity.c.)

#include <stdbool.h>
#include "cprocessing.h"

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
#define COYOTE_TIME 0.15f
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

extern Player player;

void player_physics(float dt);   // axis-separated AABB move + collide vs tiles
void hurt_player(void);          // iframe-gated damage + knockback + death reset
void player_update(float dt);    // input + jump + gravity + timers + physics + footsteps
void render_player(void);        // shadow blob + animation select + draw
