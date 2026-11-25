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
// Main-menu state (called from main.c; PLAY switches to the game state).
// Button art is freed inline in game_exit at terminate (menu_exit runs on
// every menu->game switch, so it frees nothing — the menu can be revisited).

void menu_init(void);
void menu_update(void);
void menu_exit(void);

// Menu button art (defined in menu.c; game_exit frees them at terminate)
extern CP_Image playBtnImage, volBtnImage, creditBtnImage, digipenLogoImage, buildlabLogoImage;
