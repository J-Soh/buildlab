/*
@Copyright-credits
            Third-Party Assets, Libraries & Tools:
            - Logo: BuildLab logo created by the development team.
            - Visual Assets: "Pixel Adventure 1" (player, map level, enemies, traps)
              by Pixel Frog (https://pixelfrog-assets.itch.io/pixel-adventure-1)
            - Sound Effects: step.wav, jump.wav, respawn.wav, player_hit.wav,
              hit.wav, death.wav, coin.wav, button_click.wav
              Generated using Bfxr (https://www.bfxr.net/)
            - Background Music: Sound #595750 by szegvari
              (https://freesound.org/people/szegvari/sounds/595750/)
            - Menu Music: Sound #415805 by Sunsai
              (https://freesound.org/people/Sunsai/sounds/415805/)
            - External Libraries: cJSON (cJSON.c / cJSON.h) by Dave Gamble
              (MIT License)
            - Level Design Tool: Tiled Map Editor (https://www.mapeditor.org/)
*//*__________________________________________________________________________*/

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
