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
// Learning quiz (MCQ only): every checkpoint fires one quiz the first time it
// is reached. Answer correctly -> a buff; wrong -> the quiz closes with no
// buff (skip, keep going). One quiz per checkpoint, ever.
// The game loop checks quiz_is_active() and freezes the world (like paused);
// the overlay is drawn last so it sits above everything.

#include <stdbool.h>
#include "utils.h"   // is_mouse_over, play_click, draw_cream_panel, gameFont

bool quiz_is_active(void);          // any overlay open (question panel)
void quiz_reset(void);              // fresh run: close overlay, clear toast
void quiz_start_checkpoint_quiz(void); // checkpoint reached: 1 MCQ -> buff if correct
void quiz_update(float dt);         // toast timer (keeps ticking while world frozen)
void quiz_handle_input(void);       // overlay clicks (game loop calls while active)
void quiz_draw_toast(void);         // small status banner (top-center, ~3s)
void quiz_draw_overlay(void);       // question panel
