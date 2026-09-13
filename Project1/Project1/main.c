// main.c
// Main application entry point & high-level game loop state machine.
#include "game_defs.h"
#include "audio.h"
#include "map.h"
#include "entity.h"
#include "player.h"
#include "ui.h"

// Runtime screen size and reference canvas
float screenW = DEFAULT_SCREEN_W;
float screenH = DEFAULT_SCREEN_H;
float refW    = DEFAULT_SCREEN_W;
float refH    = DEFAULT_SCREEN_H;

// State Machine for managing screens
GameState currentState = STATE_MENU;

// -----------------------------------------------------------------------------
// Lifecycle Functions
// -----------------------------------------------------------------------------

void game_init(void) {
    CP_System_SetWindowSize((int)screenW, (int)screenH);
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // Lock the loop to the display refresh rate to avoid screen tearing
    int refresh = CP_System_GetDisplayRefreshRate();
    if (refresh > 0) CP_System_SetFrameRate((float)refresh);

    // Reference canvas = the intended window size; all layout scales off this
    if (refW <= 0.0f) { refW = screenW; refH = screenH; }

    CP_System_SetWindowTitle("Pixel Adventure");

    CP_Settings_ImageMode(CP_POSITION_CORNER);
    CP_Settings_ImageFilterMode(CP_IMAGE_FILTER_NEAREST);

    bgImage = CP_Image_Load("Assets/Background/Brown.png");
    gameFont = CP_Font_Load("Assets/ThaleahFat.ttf");

    playBtnImage = CP_Image_Load("Assets/Menu/Buttons/Play.png");
    volBtnImage = CP_Image_Load("Assets/Menu/Buttons/Volume.png");
    settingsBtnImage = CP_Image_Load("Assets/Menu/Buttons/Settings.png");
    restartBtnImage = CP_Image_Load("Assets/Menu/Buttons/Restart.png");

    transitionImage = CP_Image_Load("Assets/Other/Transition.png");

    terrainSheet = CP_Image_Load("Assets/Terrain/Terrain (16x16).png");
    if (terrainSheet) {
        sheetW = (float)CP_Image_GetWidth(terrainSheet);
        sheetH = (float)CP_Image_GetHeight(terrainSheet);
    }

    // Pre-rendered map (Tiled -> Export as Image). One draw per frame; tile loop below is fallback.
    mapImage = CP_Image_Load("Assets/Terrain/map.png"); 
    if (mapImage) printf("Map image loaded: %dx%d\n", CP_Image_GetWidth(mapImage), CP_Image_GetHeight(mapImage));
    else printf("Map image missing, falling back to tile render\n");

    load_map_json("Assets/Terrain/map.json"); // map.c

    // New run starts at the first "Start" checkpoint placed in the map.
    for (int i = 0; i < entityCount; i++) {
        if (entityPool[i].kind == ENT_START) {
            startX = entityPool[i].x;
            startY = entityPool[i].y;
            break;
        }
    }

    // Virtual Guy sprites (32x32 cells, horizontal strips)
    playerIdle = CP_Image_Load("Assets/Main Characters/Virtual Guy/Idle (32x32).png");
    playerRun = CP_Image_Load("Assets/Main Characters/Virtual Guy/Run (32x32).png");
    playerJump = CP_Image_Load("Assets/Main Characters/Virtual Guy/Jump (32x32).png");
    playerDouble = CP_Image_Load("Assets/Main Characters/Virtual Guy/Double Jump (32x32).png");
    playerFall = CP_Image_Load("Assets/Main Characters/Virtual Guy/Fall (32x32).png");
    shadowImg = CP_Image_Load("Assets/Other/Shadow.png");
    dustImg = CP_Image_Load("Assets/Other/Dust Particle.png");
    heartImg = CP_Image_Load("Assets/Main Characters/Hearts.png");
    collectedImg = CP_Image_Load("Assets/Items/Fruits/Collected.png");

    // Audio: streamed music + one-shot SFX
    menuMusic = CP_Sound_LoadMusic("Assets/Sound/menu_music.wav");
    worldMusic = CP_Sound_LoadMusic("Assets/Sound/world_music.wav");
    stepSnd = CP_Sound_Load("Assets/Sound/step.wav");
    jumpSnd = CP_Sound_Load("Assets/Sound/jump.wav");
    respawnSnd = CP_Sound_Load("Assets/Sound/respawn.wav");
    clickSnd = CP_Sound_Load("Assets/Sound/button_click.wav");
    playerHitSnd = CP_Sound_Load("Assets/Sound/player_hit.wav");
    hitSnd = CP_Sound_Load("Assets/Sound/hit.wav");
    deathSnd = CP_Sound_Load("Assets/Sound/death.wav");
    coinSnd = CP_Sound_Load("Assets/Sound/coin.wav");
    windowOpenSnd = CP_Sound_Load("Assets/Sound/window_open.ogg");
    plantAtkImg = CP_Image_Load("Assets/Enemies/Plant/Attack (44x42).png");
    bulletImg = CP_Image_Load("Assets/Enemies/Plant/Bullet.png");
    piecesImg = CP_Image_Load("Assets/Enemies/Plant/Bullet Pieces.png");
    apply_volumes();

    reset_run();
    if (menuMusic) {
        CP_Sound_PlayAdvanced(menuMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
        musicMode = 1;
    }
}

void game_update(void) {
    // Keep the logical screen in sync with the fullscreen (or resized) window
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // State Logic
    if (currentState == STATE_MENU) {
        // Clear screen and draw menu background
        CP_Graphics_ClearBackground(CP_Color_Create(0, 0, 0, 255));
        bgScroll += BG_SCROLL_SPEED * CP_System_GetDt();
        if (musicMode != 1 && menuMusic) {
            CP_Sound_StopGroup(CP_SOUND_GROUP_MUSIC);
            CP_Sound_PlayAdvanced(menuMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
            musicMode = 1;
        }
        draw_background();

        draw_menu_ui();
        handle_menu_input();
        draw_transition();
    }
    else if (currentState == STATE_GAME) {
        float dt = CP_System_GetDt();
        if (dt > DT_MAX) dt = DT_MAX;  // clamp hitches, prevents tunneling

        // ESC ("exit" key): open pause menu, step back a page, or resume
        if (CP_Input_KeyTriggered(KEY_ESCAPE)) {
            if (!paused) {
                paused = true; pausePage = 0;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            }
            else if (pausePage == 1) pausePage = 0;
            else paused = false;
        }

        if (!paused) {
            if (!runStarted) { restart_from_beginning(); runStarted = true; }
            bgScroll += BG_SCROLL_SPEED * dt;
            if (musicMode != 2 && worldMusic) {
                CP_Sound_StopGroup(CP_SOUND_GROUP_MUSIC);
                CP_Sound_PlayAdvanced(worldMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
                musicMode = 2;
            }

            if (!levelComplete) {
                // --- Input: A/D move, SPACE jump/double ---
                bool left = CP_Input_KeyDown(KEY_A);
                bool right = CP_Input_KeyDown(KEY_D);
                if (player.lockT > 0.0f) player.vx = player.kbx;
                else if (left && !right) { player.vx = -MOVE_SPEED; player.facing = -1; }
                else if (right && !left) { player.vx = MOVE_SPEED; player.facing = 1; }
                else player.vx = 0.0f;

                if (CP_Input_KeyTriggered(KEY_SPACE)) player.buffer = JUMP_BUFFER;

                // --- Jump / double jump ---
                if (player.buffer > 0.0f) {
                    if (player.grounded || player.coyote > 0.0f) {
                        player.vy = JUMP_VEL;
                        player.jumps = 1;
                        player.grounded = false;
                        player.coyote = 0.0f; player.buffer = 0.0f;
                        if (jumpSnd) CP_Sound_PlayAdvanced(jumpSnd, JUMP_SFX_VOLUME, JUMP_SFX_PITCH, (CP_BOOL)0, CP_SOUND_GROUP_SFX);
                        spawn_dust(player.x + PLAYER_HB_W * 0.5f, player.y + PLAYER_HB_H - 4.0f, 0.0f, -40.0f, DUST_JUMP_LIFE, DUST_JUMP_SIZE);
                        spawn_dust(player.x + PLAYER_HB_W * 0.5f - 14.0f * (float)player.facing, player.y + PLAYER_HB_H - 4.0f, -120.0f * (float)player.facing, -40.0f, DUST_JUMP_LIFE, DUST_RUN_SIZE);
                    }
                    else if (player.jumps < 2) {
                        player.vy = DOUBLE_JUMP_VEL;
                        player.jumps = 2;
                        player.buffer = 0.0f;
                        player.doubleAnim = true; player.doubleAnimT = 0.0f;
                        if (jumpSnd) CP_Sound_PlayAdvanced(jumpSnd, JUMP_SFX_VOLUME, JUMP2_SFX_PITCH, (CP_BOOL)0, CP_SOUND_GROUP_SFX);
                        for (int k = 0; k < DOUBLE_JUMP_DUST_COUNT; k++)
                            spawn_dust(player.x + PLAYER_HB_W * 0.5f + (float)((k % 3) - 1) * 16.0f, player.y + PLAYER_HB_H - 10.0f - (float)(k / 3) * 18.0f, 0.0f, 30.0f, DUST_RUN_LIFE, DUST_JUMP2_SIZE);
                    }
                }
                if (CP_Input_KeyReleased(KEY_SPACE) && player.vy < JUMP_CUT_VY) player.vy = JUMP_CUT_VY;

                // --- Gravity ---
                player.vy += GRAVITY * dt;
                if (player.vy > MAX_FALL) player.vy = MAX_FALL;

                if (player.grounded) player.coyote = COYOTE_TIME; else player.coyote -= dt;
                player.buffer -= dt;
                player.animT += dt;
                if (player.lockT > 0.0f) { player.lockT -= dt; player.kbx -= player.kbx * 8.0f * dt; }
                if (player.invulnT > 0.0f) player.invulnT -= dt;
                if (player.doubleAnim) player.doubleAnimT += dt;

                player_physics(dt);

                // --- Footsteps + run dust ---
                if (player.grounded && (player.vx > FOOTSTEP_VX_THRESHOLD || player.vx < -FOOTSTEP_VX_THRESHOLD)) {
                    player.stepT += dt;
                    if (player.stepT >= FOOTSTEP_INTERVAL) {
                        player.stepT = 0.0f;
                        if (stepSnd) CP_Sound_Play(stepSnd);
                        spawn_dust(player.x + PLAYER_HB_W * 0.5f - 16.0f * (float)player.facing, player.y + PLAYER_HB_H - 4.0f, -60.0f * (float)player.facing, -50.0f, DUST_RUN_LIFE, DUST_RUN_SIZE);
                    }
                }
                else player.stepT = 0.0f;

                // --- Dust sim ---
                for (int i = 0; i < MAX_DUST; i++) {
                    if (!dustPool[i].active) continue;
                    dustPool[i].life -= dt;
                    if (dustPool[i].life <= 0.0f) { dustPool[i].active = false; continue; }
                    dustPool[i].x += dustPool[i].vx * dt;
                    dustPool[i].y += dustPool[i].vy * dt;
                    dustPool[i].vy -= DUST_GRAVITY * dt;
                }

                // --- Pickup sparkles ---
                for (int i = 0; i < MAX_PICKUP_FX; i++) {
                    if (!pickupFx[i].active) continue;
                    pickupFx[i].t += dt;
                    if (pickupFx[i].t >= (float)PICKUP_FRAMES / PICKUP_FPS) pickupFx[i].active = false;
                }

                // --- Camera follows player, clamped + pixel-snapped ---
                camX = player.x + PLAYER_HB_W * 0.5f - screenW * 0.5f;
                camY = player.y + PLAYER_HB_H * 0.5f - screenH * 0.5f;
                float worldW = (float)(mapWidth * (int)TILE_SIZE);
                float worldH = (float)(mapHeight * (int)TILE_SIZE);
                if (camX < 0.0f) camX = 0.0f;
                if (camY < 0.0f) camY = 0.0f;
                if (camX > worldW - screenW) camX = worldW - screenW;
                if (camY > worldH - screenH) camY = worldH - screenH;
                camX = roundf(camX);
                camY = roundf(camY);

                // Fail-safe: fell out of the world -> respawn
                if (player.y > worldH + WORLD_FALL_EXTRA || player.x < -WORLD_SIDE_EXTRA || player.x > worldW + WORLD_SIDE_EXTRA) {
                    reset_run();
                    camX = player.x + PLAYER_HB_W * 0.5f - screenW * 0.5f;
                    camY = player.y + PLAYER_HB_H * 0.5f - screenH * 0.5f;
                    if (camX < 0.0f) camX = 0.0f;
                    if (camY < 0.0f) camY = 0.0f;
                }

                update_entities(dt);
                check_player_entity_collisions();
                update_bullets(dt);
            }
        } // end of world update (skipped while paused)

        // ================= RENDER =================
        CP_Graphics_ClearBackground(CP_Color_Create(33, 31, 48, 255));
        draw_background();      // brown shows through empty (gid 0) cells
        render_map_json();      // navy tiles cover the rest
        render_entities();      // checkpoints, enemies, traps from map.json
        draw_bullets();         // plant seeds + impact pieces
        CP_Settings_ImageMode(CP_POSITION_CORNER);

        // Shadow: cast down from feet, shrink/fade with distance
        {
            float feetCX = player.x + PLAYER_HB_W * 0.5f;
            float feetY = player.y + PLAYER_HB_H;
            float groundTop = -1.0f;
            for (float wy = feetY + SHADOW_PROBE_START; wy < feetY + SHADOW_MAX_DIST; wy += SHADOW_PROBE_STEP) {
                if (is_solid_px(feetCX, wy)) {
                    groundTop = ((int)(wy / TILE_SIZE)) * TILE_SIZE;
                    break;
                }
            }
            if (shadowImg && groundTop > 0.0f) {
                float dist = groundTop - feetY;
                float k = 1.0f - dist / SHADOW_MAX_DIST;
                if (k < 0.0f) k = 0.0f;
                float sw = SHADOW_MIN_SIZE + SHADOW_SIZE_RANGE * k;
                int alpha = SHADOW_MIN_ALPHA + (int)(SHADOW_ALPHA_RANGE * k);
                float u = (float)CP_Image_GetWidth(shadowImg);
                float v = (float)CP_Image_GetHeight(shadowImg);
                CP_Image_DrawSubImage(shadowImg, roundf(feetCX - sw * 0.5f - camX), roundf(groundTop - SHADOW_ABOVE_GROUND - camY), sw, SHADOW_DRAW_HEIGHT, 0.0f, 0.0f, u, v, alpha);
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
                        frame = (int)(player.doubleAnimT * PLAYER_DOUBLE_FPS);
                        if (frame > PLAYER_DOUBLE_LAST_FRAME) frame = PLAYER_DOUBLE_LAST_FRAME;
                    }
                    else { img = playerJump; frame = 0; }
                }
                else { img = playerFall; frame = 0; }
            }
            else if (player.vx > FOOTSTEP_VX_THRESHOLD || player.vx < -FOOTSTEP_VX_THRESHOLD) {
                img = playerRun;
                frame = ((int)(player.animT * PLAYER_RUN_FPS)) % PLAYER_RUN_FRAMES;
            }
            else {
                img = playerIdle;
                frame = ((int)(player.animT * PLAYER_IDLE_FPS)) % PLAYER_IDLE_FRAMES;
            }
            int pAlpha = (player.invulnT > 0.0f && ((int)(player.invulnT * PLAYER_BLINK_RATE) % 2 == 0)) ? 130 : 255;
            draw_sprite_frame(img, frame, roundf(player.x - PLAYER_HB_OX - camX), roundf(player.y - PLAYER_HB_OY - camY), 32.0f * PLAYER_SCALE, player.facing, pAlpha);
        }

        // Dust (fade out)
        for (int i = 0; i < MAX_DUST; i++) {
            if (!dustPool[i].active || !dustImg) continue;
            float k = dustPool[i].life / dustPool[i].maxLife;
            int alpha = (int)(220.0f * k);
            float s = dustPool[i].size * (1.2f - 0.4f * k);
            float u = (float)CP_Image_GetWidth(dustImg);
            float v = (float)CP_Image_GetHeight(dustImg);
            CP_Image_DrawSubImage(dustImg, roundf(dustPool[i].x - s * 0.5f - camX), roundf(dustPool[i].y - s * 0.5f - camY), s, s, 0.0f, 0.0f, u, v, alpha);
        }

        // Pickup sparkle
        if (collectedImg) {
            for (int i = 0; i < MAX_PICKUP_FX; i++) {
                if (!pickupFx[i].active) continue;
                int frame = (int)(pickupFx[i].t * PICKUP_FPS);
                if (frame > PICKUP_FRAMES - 1) frame = PICKUP_FRAMES - 1;
                float sx = roundf(pickupFx[i].x - TILE_SIZE * 0.5f - camX);
                float sy = roundf(pickupFx[i].y - TILE_SIZE * 0.5f - camY);
                if (sx < -PICKUP_CULL_MARGIN || sx > screenW + PICKUP_CULL_MARGIN ||
                    sy < -PICKUP_CULL_MARGIN || sy > screenH + PICKUP_CULL_MARGIN) continue;
                draw_sheet_frame(collectedImg, 32, 32, frame, sx, sy, TILE_SIZE, TILE_SIZE, 1, 255);
            }
        }

        // HUD: hearts row (top-left)
        if (heartImg) {
            for (int i = 0; i < MAX_HEARTS; i++)
                draw_sheet_frame(heartImg, 16, 16, (i < hearts) ? 0 : 1, SX(16.0f) + (float)i * SX(48.0f), SY(14.0f), SX(48.0f), SY(48.0f), 1, 255);
        }

        // Level complete overlay
        if (levelComplete) {
            draw_cream_panel(screenW * 0.5f, screenH * 0.5f, SX(560.0f), SY(280.0f));
            if (gameFont) CP_Font_Set(gameFont);
            CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
            CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
            CP_Settings_TextSize(SF(58.0f));
            CP_Font_DrawText("LEVEL COMPLETE", screenW * 0.5f, SY(310.0f));
            CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
            CP_Settings_TextSize(SF(30.0f));
            CP_Font_DrawText("Click anywhere to return to menu", screenW * 0.5f, SY(400.0f));
            if (CP_Input_MouseClicked()) {
                currentState = STATE_MENU;
                runStarted = false;
                paused = false;
                levelComplete = false;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            }
        }

        if (!paused) {
            // Restart button (top-right): click = fresh run from the beginning
            if (restartBtnImage) {
                float rx = screenW - SX(64.0f);
                float ry = SY(16.0f);
                float rsize = SX(48.0f);
                bool hover = is_mouse_over(rx, ry, rsize, rsize);
                float rs = hover ? rsize + SX(6.0f) : rsize;
                float ro = (rs - rsize) * 0.5f;
                CP_Image_Draw(restartBtnImage, rx - ro, ry - ro, rs, rs, 255);
                if (hover && CP_Input_MouseClicked()) { play_click(); restart_from_beginning(); }
            }

            // Options button (top-right, left of restart): opens the pause menu
            if (settingsBtnImage) {
                float sx = screenW - SX(64.0f) - SX(56.0f);
                float sy = SY(16.0f);
                float ssize = SX(48.0f);
                bool hover = is_mouse_over(sx, sy, ssize, ssize);
                float ss = hover ? ssize + SX(6.0f) : ssize;
                float so = (ss - ssize) * 0.5f;
                CP_Image_Draw(settingsBtnImage, sx - so, sy - so, ss, ss, 255);
                if (hover && CP_Input_MouseClicked()) {
                    paused = true; pausePage = 0;
                    if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                    play_click();
                }
            }

            draw_transition();
        }
        else {
            draw_pause_menu();
            handle_pause_input();
        }
    }
}

void game_exit(void) {
    // Free allocated memory on shutdown
    if (bgImage) CP_Image_Free(bgImage);
    if (gameFont) CP_Font_Free(gameFont);

    if (playBtnImage) CP_Image_Free(playBtnImage);
    if (volBtnImage) CP_Image_Free(volBtnImage);
    if (settingsBtnImage) CP_Image_Free(settingsBtnImage);
    if (restartBtnImage) CP_Image_Free(restartBtnImage);
    if (transitionImage) CP_Image_Free(transitionImage);
    if (terrainSheet) CP_Image_Free(terrainSheet);
    if (mapImage) CP_Image_Free(mapImage);
    if (playerIdle) CP_Image_Free(playerIdle);
    if (playerRun) CP_Image_Free(playerRun);
    if (playerJump) CP_Image_Free(playerJump);
    if (playerDouble) CP_Image_Free(playerDouble);
    if (playerFall) CP_Image_Free(playerFall);
    if (shadowImg) CP_Image_Free(shadowImg);
    if (dustImg) CP_Image_Free(dustImg);
    if (heartImg) CP_Image_Free(heartImg);
    if (collectedImg) CP_Image_Free(collectedImg);
    for (int i = 0; i < entityCount; i++) {
        if (entityPool[i].baseImage) CP_Image_Free(entityPool[i].baseImage);
        if (entityPool[i].altImage) CP_Image_Free(entityPool[i].altImage);
        if (entityPool[i].idleImg) CP_Image_Free(entityPool[i].idleImg);
        if (entityPool[i].runImg) CP_Image_Free(entityPool[i].runImg);
        if (entityPool[i].hitImg) CP_Image_Free(entityPool[i].hitImg);
        if (entityPool[i].emergeImg) CP_Image_Free(entityPool[i].emergeImg);
    }
    if (plantAtkImg) CP_Image_Free(plantAtkImg);
    if (bulletImg) CP_Image_Free(bulletImg);
    if (piecesImg) CP_Image_Free(piecesImg);
    CP_Sound_StopAll();
    if (playerHitSnd) CP_Sound_Free(playerHitSnd);
    if (hitSnd) CP_Sound_Free(hitSnd);
    if (deathSnd) CP_Sound_Free(deathSnd);
    if (coinSnd) CP_Sound_Free(coinSnd);
    if (menuMusic) CP_Sound_Free(menuMusic);
    if (worldMusic) CP_Sound_Free(worldMusic);
    if (stepSnd) CP_Sound_Free(stepSnd);
    if (jumpSnd) CP_Sound_Free(jumpSnd);
    if (respawnSnd) CP_Sound_Free(respawnSnd);
    if (clickSnd) CP_Sound_Free(clickSnd);
    if (windowOpenSnd) CP_Sound_Free(windowOpenSnd);
    if (mapData) free(mapData);
}

int main(void) {
    CP_Engine_SetNextGameState(game_init, game_update, game_exit);
    CP_Engine_Run(1); // opens console window (for debug print messages)
    return 0;
}