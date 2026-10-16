#pragma once
// Enemy + fruit behavior and render: bat/chicken/rino/plant AI, fruits,
// plant seeds, entity draw, and the player<->entity seam
// (check_player_entity_collisions — the other half lives in hurt_player,
// player.h). Static world objects (traps, checkpoints) ride the same pool.
// (Types + pool + spawn stay in extract.h — extraction fills them, this reads them.)

#include "cprocessing.h"

#define MAX_BULLETS 12
typedef struct { bool active; float x, y, vx, vy, life; } Bullet;
extern Bullet bullets[MAX_BULLETS];
#define MAX_BULLET_FX 8
typedef struct { bool active; float x, y, t; } BulletFX;
extern BulletFX bulletFx[MAX_BULLET_FX];
#define BULLET_SPEED 420.0f

void update_entities(float dt);             // enemy AI + enemy physics (bat/chicken/rino/plant)
void render_entities(void);                 // checkpoints, enemies, traps, fruits
void check_player_entity_collisions(void);  // checkpoints, stomp, hazards, fruits
void update_bullets(float dt);              // plant seeds fly, pop, hurt
void draw_bullets(void);                    // plant seeds + impact pieces
