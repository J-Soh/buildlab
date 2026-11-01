#pragma once
// Enemy + fruit behavior and render: chicken/rino AI, fruits, entity draw,
// and the player<->entity seam
// (check_player_entity_collisions — the other half lives in hurt_player,
// player.h). Static world objects (traps, checkpoints) ride the same pool.
// (Types + pool + spawn stay in extract.h — extraction fills them, this reads them.)

#include "cprocessing.h"

void update_entities(float dt);             // chicken/rino AI + enemy physics
void render_entities(void);                 // checkpoints, enemies, traps, fruits
void check_player_entity_collisions(void);  // checkpoints, stomp, hazards, fruits
