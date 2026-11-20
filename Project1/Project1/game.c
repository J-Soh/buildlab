// memory leak checker
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cprocessing.h"
#include "utils.h"  // shared canvas, assets, UI helpers (screen vars, font, sounds, cream panel)
#include "menu.h"   // menu_init/menu_update/menu_exit (pause MENU + level-complete return here)
#include "game.h"   // own state entry points + shared render state (mapImage, camX/camY...)
#include "extract.h" // map extraction: entity types/pool, load_map_json, render_map_json
#include "pause.h"   // pause menu state (paused/pausePage) + draw/handle entry points
#include "player.h"  // player state + update/physics/render entry points
#include "entity.h" // enemy AI/render, player<->entity collisions
#include "skin.h"    // selected skin (reload_player_skin) + picker preview assets
#include "quiz.h"    // learning quiz overlays (end quiz/buffs/death quiz + toast)

// Gameplay HUD button (menu buttons moved to menu.c;
// shared canvas/assets/helpers moved to utils.c)
static CP_Image restartBtnImage = NULL;  // gameplay HUD, top-right

// Corner buttons use a fixed 1600x900 top-right layout (48px, no scaling).

// Terrain Tilesheet Handle & Dimensions (fallback if map.png is missing;
// shared with render_map_json in extract.c via game.h)
CP_Image terrainSheet = NULL;
float sheetW = 384.0f; // Width of Terrain (16x16).png

// Pre-rendered map (Tiled -> Export as Image at 1x = 3200x1600).
// Drawn as one camera sub-rect per frame; map.json tile data stays for collision only.
// (Shared with render_map_json in extract.c via game.h; freed in game_exit.)
CP_Image mapImage = NULL;

// Tilemap Camera (world-pixel position of the viewport's top-left corner;
// shared with render_map_json in extract.c via game.h)
float camX = 0.0f;
float camY = 0.0f;

// Hit-feel (extern in game.h; entity.c sets them on stomps/kills):
// hitStopT freezes the sim ~6 frames, shakeT jitters the camera ~0.18s
float hitStopT = 0.0f;
float shakeT = 0.0f;

CP_Image playerIdle = NULL;    // 11 frames (extern in game.h; render_player in player.c reads them)
CP_Image playerRun = NULL;     // 12 frames
CP_Image playerJump = NULL;    // 1 frame
CP_Image playerDouble = NULL;  // 6 frames
CP_Image playerFall = NULL;    // 1 frame
CP_Image shadowImg = NULL;
static CP_Image dustImg = NULL;

static CP_Sound worldMusic = NULL;
CP_Sound jumpSnd = NULL;  // extern in game.h; player_handle_input in player.c plays it
CP_Sound respawnSnd = NULL;    // every player spawn (extern in game.h; quiz.c survival plays it)
// (stepSnd + hit/death/coin/playerHit sounds now owned by utils.c — collision plays them)

// Hearts HUD (hearts counter now owned by utils.c; art stays here)
static CP_Image heartImg = NULL;  // 16x16 cells: 0 full, 1 empty

static float startX = -1.0f, startY = -1.0f;      // player CENTER, world px (level start from ENT_START; never cleared)

// Fruit pickup sparkle: Collected.png (32x32 cells, 6-frame one-shot, like
// PixelAdventure's CollectedAnimation). Spawned where a fruit is collected,
// plays once, then frees its slot.
static CP_Image collectedImg = NULL;
// (pickup FX pool + spawner now owned by utils.c; game.c sims/draws the sparkle)

static bool instructionsOpen = false;

static void handle_instructions_input(void) {
    if (!instructionsOpen) return;

    float cx = SCREEN_WIDTH * 0.5f;
    float cy = SCREEN_HEIGHT * 0.5f;
    float panelW = SCREEN_WIDTH < 1200.0f ? SCREEN_WIDTH - 80.0f : 1120.0f;
    float panelH = SCREEN_HEIGHT < 760.0f ? SCREEN_HEIGHT - 60.0f : 720.0f;
    float buttonW = 280.0f;
    float buttonH = 64.0f;
    float buttonX = cx - buttonW * 0.5f;
    float buttonY = cy + panelH * 0.5f - 96.0f;

    if (CP_Input_KeyTriggered(KEY_ENTER) ||
        (CP_Input_MouseClicked() &&
         is_mouse_over(buttonX, buttonY, buttonW, buttonH))) {
        instructionsOpen = false;
        play_click();
    }
}

static void draw_instructions_overlay(void) {
    if (!instructionsOpen) return;

    float cx = SCREEN_WIDTH * 0.5f;
    float cy = SCREEN_HEIGHT * 0.5f;
    float panelW = SCREEN_WIDTH < 1200.0f ? SCREEN_WIDTH - 80.0f : 1120.0f;
    float panelH = SCREEN_HEIGHT < 760.0f ? SCREEN_HEIGHT - 60.0f : 720.0f;

    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 170));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);
    draw_cream_panel(cx, cy, panelW, panelH);

    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(44.0f);
    CP_Font_DrawText("HOW TO PLAY", cx, cy - panelH * 0.5f + 58.0f);

    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_LEFT, CP_TEXT_ALIGN_V_MIDDLE);
    CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
    CP_Settings_TextSize(25.0f);

    float left = cx - panelW * 0.5f + 75.0f;
    float y = cy - panelH * 0.5f + 130.0f;
    float lineGap = panelH > 680.0f ? 68.0f : 56.0f;

    CP_Font_DrawText("A / D - Move left and right", left, y);
    CP_Font_DrawText("SPACE - Jump; double-tap for double jump", left, y += lineGap);
    CP_Font_DrawText("Hold SPACE - Jump higher", left, y += lineGap);
    CP_Font_DrawText("Rinos and Chickens are enemies", left, y += lineGap);
    CP_Font_DrawText("Jump on top of enemies to defeat them", left, y += lineGap);
    CP_Font_DrawText("Fruits give 50 points", left, y += lineGap);
    CP_Font_DrawText("Defeating enemies gives 100 points", left, y += lineGap);
    CP_Font_DrawText("Checkpoints trigger quizzes; correct answers give a buff", left, y += lineGap);
    CP_Font_DrawText("Death returns you to the latest checkpoint and can grant another buff", left, y += lineGap);

    float buttonW = 280.0f;
    float buttonH = 64.0f;
    float buttonX = cx - buttonW * 0.5f;
    float buttonY = cy + panelH * 0.5f - 96.0f;
    bool hover = is_mouse_over(buttonX, buttonY, buttonW, buttonH);

    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(hover ? CP_Color_Create(215, 140, 40, 90) : CP_Color_Create(40, 40, 40, 20));
    CP_Graphics_DrawRect(buttonX, buttonY, buttonW, buttonH);
    CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
    CP_Settings_TextSize(30.0f);
    CP_Font_DrawText("LET'S PLAY", cx, buttonY + buttonH * 0.5f);
}

// Leftmost lowest open pocket with headroom + floor: adapts to map edits
static void find_spawn_cell(int* outTx, int* outTy) {
    if (!mapData || mapWidth <= 0 || mapHeight <= 0) { *outTx = 0; *outTy = 0; return; }
    for (int tx = 0; tx < mapWidth; tx++) { // search left to right (COLUMNS)
        for (int ty = mapHeight - 1; ty >= 1; ty--) { // search bottom to top (ROWS)
            // Since mapData is 1D array, we need convert into 2D array:
            // Formula: index = row (ty) * number_of_columns (mapWidth) + column (tx)
            // i = 3 * 5 + 2 = 17. So mapData[17] is the tile at column 2, row 3.
            int i = ty * mapWidth + tx; 
            // Checks if tile above: empty (headroom), player tile: empty (player occupies here), tile below: solid (floor)
            if (mapData[i] == 0 && mapData[(ty - 1) * mapWidth + tx] == 0 && is_solid_cell(tx, ty + 1)) {
                *outTx = tx; *outTy = ty; // found spawn position
                return; // stop searching
            }
        }
    }
    // fallback: this only runs if above fails to find desired spawn location.
    *outTx = mapWidth / 2; *outTy = mapHeight / 2;
}

// Fresh start from the beginning: forget checkpoints, then reset.
// (reset_run alone respawns at the last checkpoint — that stays for death and
// fell-out-of-world rescues. RESTART buttons and menu PLAY use this instead,
// so flags, fruits, enemies AND spawn all start over.)
void reset_run(void);  // defined just below; fwd so the helper can call it (non-static: hurt_player in utils.c calls it)
void restart_from_beginning(void) {
    respawnX = -1.0f; respawnY = -1.0f;
    score = 0; combo = 1; checkpointScore = 0; // points economy fresh
    maxHearts = MAX_HEARTS;                     // undo Fortitude
    moveSpeedMul = 1.0f; enemySpeedMul = 1.0f;  // undo buffs' muls
    buffs.fortitude = false; buffs.swift = false;
    quiz_reset();                               // forget topics, close overlays
    hitStopT = 0.0f; shakeT = 0.0f;
    reset_run();
}

void reset_run(void) {
    if (respawnX > 0.0f && respawnY > 0.0f) {
        player.x = roundf(respawnX - PLAYER_HB_W * 0.5f);
        player.y = roundf(respawnY - PLAYER_HB_H * 0.5f);
    } else if (startX > 0.0f && startY > 0.0f) {
        player.x = roundf(startX - PLAYER_HB_W * 0.5f);
        player.y = roundf(startY - PLAYER_HB_H * 0.5f);
    } else {
        int tx, ty;
        find_spawn_cell(&tx, &ty); // find spawn location (bottom left conner)
        player.x = tx * TILE_SIZE + (TILE_SIZE - PLAYER_HB_W) * 0.5f;
        player.y = (ty + 1) * TILE_SIZE - PLAYER_HB_H - 1.0f;
    }
    player.vx = 0.0f; player.vy = 0.0f;
    player.grounded = false; player.jumps = 0; player.facing = 1;
    player.coyote = 0.0f; player.buffer = 0.0f;
    player.animT = 0.0f; player.stepT = 0.0f;
    player.doubleAnim = false; player.doubleAnimT = 0.0f;
    player.lockT = 0.0f; player.kbx = 0.0f;
    player.invulnT = 0.0f;
    hearts = maxHearts;          // checkpoint respawn restores the (possibly buffed) cap
    score = checkpointScore;     // death rolls points back to the last checkpoint (or 0 at spawn)
    combo = 1;                   // airborne fruit combo resets on respawn
    levelComplete = false;
    for (int i = 0; i < MAX_POPUPS; i++) popups[i].active = false;
    for (int i = 0; i < MAX_DUST; i++) dustPool[i].active = false;
    for (int i = 0; i < MAX_PICKUP_FX; i++) pickupFx[i].active = false;

    // Re-arm every extracted map object (chicken, rino, trap etc) at its spawn point (death / restart)
    // vx = velocity x-axis movement
    // Entity structure defined in extract.h
    for (int i = 0; i < entityCount; i++) {
        Entity* e = &entityPool[i];
        const EntityDef* def = &ENTITY_DEFS[e->kind]; // kind = chicken, rino, trap etc
        e->active = true;
        e->x = e->sx; e->y = e->sy;
        if (e->kind == ENT_CHICKEN || e->kind == ENT_RINO) {
            e->vx = 0.0f;  // facing kept from Tiled spawn; update loop drives movement
        } else {
            e->vx = (def->speed > 0.0f) ? (((i & 1) == 0) ? -def->speed : def->speed) : 0.0f;
            e->facing = e->vx >= 0.0f ? 1 : -1; // Facing towards right if movement more than 0
        }
        e->vy = 0.0f;
        e->grounded = false;
        e->activated = false;
        e->animT = 0.0f;
        e->animState = 1; e->stateT = 0.0f; e->deathT = 0.0f;
    }

    if (respawnSnd) CP_Sound_Play(respawnSnd); // play sound whenever player respawn
    // Keep player centered on screen = player HitBox center - screen center
    camX = player.x + PLAYER_HB_W * 0.5f - SCREEN_WIDTH * 0.5f; // 
    camY = player.y + PLAYER_HB_H * 0.5f - SCREEN_HEIGHT * 0.5f;
}
  
// -----------------------------------------------------------------------------
// Player skin art
// -----------------------------------------------------------------------------
// One animation strip per skin folder. Every file name is identical across the
// four characters, so only the folder changes when the player picks a skin.
static CP_Image load_skin_strip(const char* folder, const char* file) {
    char path[256];
    snprintf(path, sizeof(path), "Assets/Main Characters/%s/%s", folder, file);
    return CP_Image_Load(path);
}

// Free the four non-idle strips and NULL them. The IDLE portrait is borrowed
// from skin.c (the picker already loads one per skin), because CProcessing
// returns the same image handle for the same path — freeing it here would
// dangle the picker card for the previous skin. skin_free_assets owns it.
// Safe to call repeatedly (game_exit and reload_player_skin both use it).
void unload_player_skin(void) {
    if (playerRun) CP_Image_Free(playerRun);
    if (playerJump) CP_Image_Free(playerJump);
    if (playerDouble) CP_Image_Free(playerDouble);
    if (playerFall) CP_Image_Free(playerFall);
    playerIdle = NULL; playerRun = NULL; playerJump = NULL;
    playerDouble = NULL; playerFall = NULL;
}

// Load the currently selected skin (skinIndex, default 0 = blue astronaut).
void reload_player_skin(void) {
    skin_init_assets();  // guarantee the shared idle portraits (and playerIdle) exist
    unload_player_skin();
    const SkinDef* skin = skin_def(skinIndex);
    playerIdle = skin_preview_idle(skinIndex);  // borrowed from skin.c (idle strip)
    playerRun = load_skin_strip(skin->folder, "Run (32x32).png");
    playerJump = load_skin_strip(skin->folder, "Jump (32x32).png");
    playerDouble = load_skin_strip(skin->folder, "Double Jump (32x32).png");
    playerFall = load_skin_strip(skin->folder, "Fall (32x32).png");
}

// -----------------------------------------------------------------------------
// Lifecycle Functions
// -----------------------------------------------------------------------------

void game_init(void) {
    // Window + menu/shared assets are owned by menu_init (menu always runs first).
    // This loads only gameplay assets here (guards skip anything already loaded,
    // since quitting to menu and back re-enters this state).
    CP_Settings_ImageMode(CP_POSITION_CORNER);
    CP_Settings_ImageFilterMode(CP_IMAGE_FILTER_NEAREST); // make image look pixelated (no blur) when scaled up

    // Full gameplay loads (game_exit frees + NULLs everything on every exit,
    // so each entry reloads cleanly with no leaks or duplicated entities).
    restartBtnImage = CP_Image_Load("Assets/Menu/Buttons/Restart.png");

    terrainSheet = CP_Image_Load("Assets/Terrain/Terrain (16x16).png");
    if (terrainSheet) {
        sheetW = (float)CP_Image_GetWidth(terrainSheet);
    }

    // Pre-rendered map (Tiled -> Export as Image). One draw per frame; tile loop below is fallback.
    mapImage = CP_Image_Load("Assets/Terrain/map.png");
    if (mapImage) printf("Map image loaded: %dx%d\n", CP_Image_GetWidth(mapImage), CP_Image_GetHeight(mapImage));
    else printf("Map image missing, falling back to tile render\n");

    load_map_json("Assets/Terrain/map.json");

    // New run starts at the first "Start" checkpoint placed in the map.
    // Stored separately from the checkpoint respawn so fresh starts survive
    // restart_from_beginning() clearing respawnX/Y (that wipe caused the
    // player to fall back to find_spawn_cell and drop out of the map).
    for (int i = 0; i < entityCount; i++) {
        if (entityPool[i].kind == ENT_START) {
            startX = entityPool[i].x;
            startY = entityPool[i].y;
            break;
        }
    }

    // Player sprites for the chosen skin (32x32 cells, horizontal strips).
    // Defaults to the blue astronaut; the pause menu can change it mid-run.
    reload_player_skin();
    shadowImg = CP_Image_Load("Assets/Other/Shadow.png");
    dustImg = CP_Image_Load("Assets/Other/Dust Particle.png");
    heartImg = CP_Image_Load("Assets/Main Characters/Hearts.png");
    collectedImg = CP_Image_Load("Assets/Items/Fruits/Collected.png");  // 32x32 x6 pickup sparkle

    // Audio: streamed music + one-shot SFX
    // (menuMusic/clickSnd/windowOpenSnd are menu-owned in menu_init — not reloaded here)
    worldMusic = CP_Sound_LoadMusic("Assets/Sound/world_music.wav");
    stepSnd = CP_Sound_Load("Assets/Sound/step.wav");
    jumpSnd = CP_Sound_Load("Assets/Sound/jump.wav");
    respawnSnd = CP_Sound_Load("Assets/Sound/respawn.wav");
    playerHitSnd = CP_Sound_Load("Assets/Sound/player_hit.wav");
    hitSnd = CP_Sound_Load("Assets/Sound/hit.wav");
    deathSnd = CP_Sound_Load("Assets/Sound/death.wav");
    coinSnd = CP_Sound_Load("Assets/Sound/coin.wav");
    apply_volumes(); // utils.c

    restart_from_beginning();  // fresh run on every entry (Game_Init runs per entry now)
    instructionsOpen = true;
}

// Pause menu (pause_row_y, draw_pause_menu, handle_pause_input) now owned by
// pause.c — the game loop below calls draw/handle via pause.h.

void game_update(void) {
    // Keep the logical screen in sync with the fullscreen (or resized) window
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // Game state only now (menu lives in menu.c; the engine switches states,
    // so the old STATE_MENU / STATE_GAME switch is gone).
    {
        float dt = CP_System_GetDt();
        if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;  // prevents background tearing

        if (instructionsOpen) {
            handle_instructions_input();
        }

        // ESC ("exit" key): when a quiz overlay is open it owns input; else
        // cancel confirm popup, open pause menu, step back a page, or resume
        if (!instructionsOpen && CP_Input_KeyTriggered(KEY_ESCAPE) && !quiz_is_active() && !levelComplete) {
            if (pause_confirm_open()) pause_cancel_confirm();  // ESC closes popup, stays paused
            else if (!paused) {
                paused = true; pausePage = 0;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            }
            else if (pausePage != 0) pausePage = 0;  // back out of OPTIONS / SKINS
            else paused = false;
        }

        if (!instructionsOpen && !paused && !quiz_is_active()) {
            bgScroll += BG_SCROLL_SPEED * dt; // background scrolls while the world is active (paused freezes it)
            if (musicMode != 2 && worldMusic) {
                CP_Sound_StopGroup(CP_SOUND_GROUP_MUSIC);
                CP_Sound_PlayAdvanced(worldMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
                musicMode = 2;
            }

            if (!levelComplete) {
            if (hitStopT > 0.0f) {
                hitStopT -= dt;  // freeze sim a beat on stomps/kills (camera keeps drawing)
            } else {
            player_update(dt);  // input + timers + physics + footsteps (player.c)

            // --- Dust sim --- (defined in utils.c; game.c updates it while the world is active)
            for (int i = 0; i < MAX_DUST; i++) {
                if (!dustPool[i].active) continue; 
                dustPool[i].life -= dt;  // dust particles fade out over time (life counts down from 0.4 to 0.0)
                if (dustPool[i].life <= 0.0f) { dustPool[i].active = false; continue; }
                dustPool[i].x += dustPool[i].vx * dt;
                dustPool[i].y += dustPool[i].vy * dt; 
                dustPool[i].vy -= 300.0f * dt; // makes dust float up then fall down (gravity)
            }

            // --- Fruits Pickup sparkles ---
            // t increases every frame (dt) and after 6 frames popups[i].active = false to free slot.
            for (int i = 0; i < MAX_PICKUP_FX; i++) {
                if (!pickupFx[i].active) continue;
                pickupFx[i].t += dt;
                // t = 0 -> frame 0
                // t = 0.04 -> frame 1
                // t = 0.28 -> frame 5
                // t = 0.3 -> frame 6 (end of animation, free slot)
                if (pickupFx[i].t >= (float)PICKUP_FRAMES / PICKUP_FPS) pickupFx[i].active = false;
            } // PICKUP_FRAMES / PICKUP_FPS = 6 / 18 = 0.33333

            // --- Score popups (+50 / -25) float up and fade (pool in utils.c) ---
            // t = 0, popup floats upward.
            // t increases every frame (dt) and after 0.9 seconds popups[i].active = false to free slot.
            // while popups[i].active is still active, it is being printed out in-game for 0.9 seconds (line 401 & 434)
            for (int i = 0; i < MAX_POPUPS; i++) {
                if (!popups[i].active) continue;
                popups[i].t += dt; // t = 0 -> popup just spawned, t = 0.9 -> popup disappears
                popups[i].y -= 46.0f * dt; // popup floats up at 46px/sec
                if (popups[i].t >= 0.9f) popups[i].active = false;
            }

            // --- Camera follows player, clamped + pixel-snapped ---
            // player.x = player's hitbox top-left x position in world pixels
            // PLAYER_HB_W * 0.5f = half of player's hitbox width (center x offset)
            // player center = player.x + PLAYER_HB_W * 0.5f
            camX = player.x + PLAYER_HB_W * 0.5f - SCREEN_WIDTH * 0.5f; // cetner x of player 
            camY = player.y + PLAYER_HB_H * 0.5f - SCREEN_HEIGHT * 0.5f; // center y of player
            float worldW = (float)(mapWidth * (int)TILE_SIZE);
            float worldH = (float)(mapHeight * (int)TILE_SIZE);
            // prevent camera from going outside the world boundaries (left/top)
            if (camX < 0.0f) camX = 0.0f;
            if (camY < 0.0f) camY = 0.0f;
            // Clamp camera to the world boundaries (prevent camera from going outside the world)
            if (camX > worldW - SCREEN_WIDTH) camX = worldW - SCREEN_WIDTH;
            if (camY > worldH - SCREEN_HEIGHT) camY = worldH - SCREEN_HEIGHT;
            camX = roundf(camX);
            camY = roundf(camY);

            // Fail-safe: fell out of the world somehow -> respawn, never stranded
            if (player.y > worldH + 200.0f || player.x < -600.0f || player.x > worldW + 600.0f) {
                reset_run();
                camX = player.x + PLAYER_HB_W * 0.5f - SCREEN_WIDTH * 0.5f;
                camY = player.y + PLAYER_HB_H * 0.5f - SCREEN_HEIGHT * 0.5f;
                if (camX < 0.0f) camX = 0.0f;
                if (camY < 0.0f) camY = 0.0f;
            }

            /*
            (entity.c) constantly checks and updates on:
            - check if player stomp enemy
            - enemy hit player
            - Chicken/Rino patrol walk left / right
            */
            update_entities(dt);

            // (entity.c) checks if player reach checkpoints, collect fruits,
            // stomp enemies or take dmg.
            check_player_entity_collisions();
            }
            }
        } // end of world update (skipped while paused)

        // Quiz overlays keep ticking while the world is frozen (quiz freezes the sim)
        quiz_update(dt);
        if (quiz_is_active() && !paused) quiz_handle_input();

        // ================= RENDER =================
        CP_Graphics_ClearBackground(CP_Color_Create(33, 31, 48, 255));
        draw_background();      // brown shows through empty (gid 0) cells

        // Screen shake: nudge the world camera for a few frames around hit-feel
        float savedCamX = camX, savedCamY = camY;
        if (shakeT > 0.0f) {
            shakeT -= dt;
            if (shakeT < 0.0f) shakeT = 0.0f;
            camX += CP_Random_RangeFloat(-6.0f, 6.0f);
            camY += CP_Random_RangeFloat(-6.0f, 6.0f);
        }

        render_map_json();      // loads game level map (extract.c)
        render_entities();      // checkpoints, enemies, traps, fruits (entity.c)
        CP_Settings_ImageMode(CP_POSITION_CORNER);

        render_player();  // shadow + animation select + draw (player.c)

        // Dust (fade out) image played whenever player runs or jump on top of enemies.
        for (int i = 0; i < MAX_DUST; i++) {
            if (!dustPool[i].active || !dustImg) continue;
            float k = dustPool[i].life / dustPool[i].maxLife; // life ratio (1.0 = full life, 0.0 = no more dust)
            int alpha = (int)(220.0f * k); // fade out dust as it dies
            float s = dustPool[i].size * (1.2f - 0.4f * k);
            float u = (float)CP_Image_GetWidth(dustImg);
            float v = (float)CP_Image_GetHeight(dustImg);
            CP_Image_DrawSubImage(dustImg, roundf(dustPool[i].x - s * 0.5f - camX), roundf(dustPool[i].y - s * 0.5f - camY), s, s, 0.0f, 0.0f, u, v, alpha);
        }

        // Pickup sparkle (Collected one-shot, drawn at the fruit's TILE_SIZE footprint)
        if (collectedImg) {
            for (int i = 0; i < MAX_PICKUP_FX; i++) {
                if (!pickupFx[i].active) continue;
                // PICKUP_FPS = 18.0f, PICKUP_FRAMES = 6, so frame = t * 18.0f
                // Each frame is 1/18th of a second, so 6 frames = 6/18 = 1/3 second
                // t = 0.0 -> frame 0
                // t = 0.04 -> frame 1
                // t = 0.28 -> frame 5  (final frame, then pickupFx[i].active = false)
                int frame = (int)(pickupFx[i].t * PICKUP_FPS);
                if (frame > PICKUP_FRAMES - 1) frame = PICKUP_FRAMES - 1;
                float sx = roundf(pickupFx[i].x - TILE_SIZE * 0.5f - camX);
                float sy = roundf(pickupFx[i].y - TILE_SIZE * 0.5f - camY);
                if (sx < -64.0f || sx > SCREEN_WIDTH + 64.0f ||
                    sy < -64.0f || sy > SCREEN_HEIGHT + 64.0f) continue;
                draw_sheet_frame(collectedImg, 32, 32, frame, sx, sy, TILE_SIZE, TILE_SIZE, 1, 255);
            }
        }

        // HUD: score + hearts row (top-left)
        if (gameFont) CP_Font_Set(gameFont);
        CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_LEFT, CP_TEXT_ALIGN_V_MIDDLE);
        CP_Settings_Fill(CP_Color_Create(245, 235, 210, 255)); // light cream (off-white) for text
        CP_Settings_TextSize(30.0f);
        {
            // High score on the left, above the current score
            char scoreText[32];
            char highText[32];
            // snprintf(destination buffer, max size, "format %d", integer replacing %d);
            snprintf(highText, sizeof(highText), "HIGH SCORE %d", highScore);
            CP_Font_DrawText(highText, 16.0f, 78.0f);
            snprintf(scoreText, sizeof(scoreText), "CURRENT SCORE  %d", score);
            CP_Font_DrawText(scoreText, 16.0f, 110.0f);
        }
        if (heartImg) {
            for (int i = 0; i < maxHearts; i++) // maxHeart = 5 (defined in utils.h)
            // draw_sheet_frame(image, frameWidth, frameHeight, frameIndex, x, y, width, height, scale, alpha)
            // Draw full heart if i < hearts (0), else draw empty heart (1)
            // i = 0 → full
            // i = 1 → full
            // i = 2 → empty ( if hearts = 2 which player decreased health from player.c (hurt_player()))
                draw_sheet_frame(heartImg, 16, 16, (i < hearts) ? 0 : 1, 16.0f + (float)i * 48.0f, 14.0f, 48.0f, 48.0f, 1, 255);
        }

        // Score popups float up from where the points were earned (world space)
        if (gameFont) CP_Font_Set(gameFont);
        CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
        for (int i = 0; i < MAX_POPUPS; i++) {
            if (!popups[i].active) continue;
            // t = 0.0 -> alpha = 255, fully visible
            // t = 0.45 -> alpha ≈ 127, half transparent
            // t = 0.9  -> alpha = 0, invisible and deactivated
            int alpha = (int)(255.0f * (1.0f - popups[i].t / 0.9f));
            if (alpha < 0) alpha = 0;
            // neg = true (amount < 0) → red, else yellow (amount > 0)
            bool neg = popups[i].amount < 0;
            CP_Settings_Fill(neg ? CP_Color_Create(230, 70, 60, alpha) // red
                                 : CP_Color_Create(255, 225, 120, alpha)); // yellow
            CP_Settings_TextSize(26.0f);
            char buf[16];
            snprintf(buf, sizeof(buf), "%+d", popups[i].amount);
            // as screen moves, popups[i].x/y are in world space, so subtract camX/Y to convert to screen space (so it would appear in correct position on screen)
            CP_Font_DrawText(buf, popups[i].x - camX, popups[i].y - camY);
        }

        // Shake end: HUD/overlays/buttons sit on the steady camera
        camX = savedCamX;
        camY = savedCamY;

        quiz_draw_toast();  // checkpoint-quiz status banner (top-center, ~3s) (quiz.c)

        // Level complete overlay (End object reached)
        if (levelComplete) {
            draw_cream_panel(SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f, 560.0f, 280.0f);
            if (gameFont) CP_Font_Set(gameFont);
            CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
            CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
            CP_Settings_TextSize(58.0f);
            CP_Font_DrawText("LEVEL COMPLETE", SCREEN_WIDTH * 0.5f, 400.0f);
            {
                char scoreText[32];
                char highText[48];
                CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
                CP_Settings_TextSize(30.0f);
                snprintf(scoreText, sizeof(scoreText), "SCORE  %d", score);
                CP_Font_DrawText(scoreText, SCREEN_WIDTH * 0.5f, 448.0f);
                snprintf(highText, sizeof(highText), "%s  %d",
                         newHighScore ? "NEW HIGH SCORE" : "HIGH SCORE", highScore);
                CP_Settings_Fill(newHighScore
                                 ? CP_Color_Create(215, 140, 40, 255)
                                 : CP_Color_Create(120, 120, 120, 255));
                CP_Font_DrawText(highText, SCREEN_WIDTH * 0.5f, 484.0f);
            }
            CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
            CP_Settings_TextSize(26.0f);
            CP_Font_DrawText("Click anywhere to return to menu", SCREEN_WIDTH * 0.5f, 545.0f);
            if (CP_Input_MouseClicked()) {
                CP_Engine_SetNextGameState(menu_init, menu_update, menu_exit);  // back to title
                paused = false;
                levelComplete = false;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            }
        }

        if (instructionsOpen) {
            // Instructions freeze the new run until the player dismisses the card.
        }
        else if (!paused && !quiz_is_active()) { // if paused or quiz overlay is not active
                // Restart button (top-right): click = fresh run from the beginning
                if (restartBtnImage) {
                    float rx = SCREEN_WIDTH - 64.0f;
                    float ry = 16.0f;
                    float rsize = 48.0f;
                    bool hover = is_mouse_over(rx, ry, rsize, rsize);
                    // normal size = 48x48, hover size = 54x54 (enlarged by 6 pixels)
                    float rs = hover ? rsize + 6.0f : rsize;
                    float ro = (rs - rsize) * 0.5f; // keep button centered when it grows on hover
                    // Subtracting ro moves the enlarged image 3 pixels left and 3 pixels up, making it appear centered on the original position after it is enlarged.
                    CP_Image_Draw(restartBtnImage, rx - ro, ry - ro, rs, rs, 255);
                    if (hover && CP_Input_MouseClicked()) { play_click(); restart_from_beginning(); }
                }

            // Options button (top-right, left of restart): opens the pause menu
            if (settingsBtnImage) {
                float sx = SCREEN_WIDTH - 64.0f - 56.0f;
                float sy = 16.0f;
                float ssize = 48.0f;
                bool hover = is_mouse_over(sx, sy, ssize, ssize);
                // normal size = 48x48, hover size = 54x54 (enlarged by 6 pixels)
                float ss = hover ? ssize + 6.0f : ssize;
                float so = (ss - ssize) * 0.5f; // keep button centered when it grows on hover
                // Subtracting so moves the enlarged image 3 pixels left and 3 pixels up, making it appear centered on the original position after it is enlarged
                CP_Image_Draw(settingsBtnImage, sx - so, sy - so, ss, ss, 255);
                if (hover && CP_Input_MouseClicked()) {
                    paused = true; pausePage = 0;
                    if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                    play_click();
                }
            }
        }
        else { // if paused or quiz overlay is active, draw the pause menu (or quiz overlay) on top of the game world
            // Pause menu (pause.c) draws the cream panel, buttons, and handles input.
            draw_pause_menu();
            // Check if user click on the pause menu buttons (resume, options, quit) 
            handle_pause_input();
        }

        quiz_draw_overlay();  // quiz/buff panels sit above HUD + pause
        draw_instructions_overlay();
    }
}

void game_exit(void) {
    // Free allocated memory on shutdown. menu_exit runs on every menu->game
    // switch (menu can be revisited) so it frees nothing — menu button art
    // (extern from menu.h) is freed inline here, once, at terminate.
    // NOTE: game_exit ALSO runs on every quit-to-menu (engine exits the old
    // state on switch), so every pointer freed here is set back to NULL and
    // entityCount to 0. Otherwise menu_init/game_init reload guards see stale
    // non-NULL handles, skip reloading, and the menu draws freed memory
    // (that use-after-free was the finish-game crash).
    if (playBtnImage) CP_Image_Free(playBtnImage);
    if (volBtnImage) CP_Image_Free(volBtnImage);
    playBtnImage = NULL; volBtnImage = NULL;
    if (creditBtnImage) CP_Image_Free(creditBtnImage);
    if (digipenLogoImage) CP_Image_Free(digipenLogoImage);
    creditBtnImage = NULL; digipenLogoImage = NULL;
    if (buildlabLogoImage) CP_Image_Free(buildlabLogoImage);
    buildlabLogoImage = NULL;
    if (bgImage) CP_Image_Free(bgImage);
    if (gameFont) CP_Font_Free(gameFont);
    bgImage = NULL; gameFont = NULL;

    if (settingsBtnImage) CP_Image_Free(settingsBtnImage);
    if (restartBtnImage) CP_Image_Free(restartBtnImage);
    settingsBtnImage = NULL; restartBtnImage = NULL;
    if (terrainSheet) CP_Image_Free(terrainSheet);
    if (mapImage) CP_Image_Free(mapImage);
    terrainSheet = NULL; mapImage = NULL;
    unload_player_skin();  // frees the four owned strips (idle portrait belongs to skin.c)
    skin_free_assets();    // frees the idle portraits (reloaded by menu_init on return)
    if (shadowImg) CP_Image_Free(shadowImg);
    if (dustImg) CP_Image_Free(dustImg);
    if (heartImg) CP_Image_Free(heartImg);
    if (collectedImg) CP_Image_Free(collectedImg);
    shadowImg = NULL; dustImg = NULL; heartImg = NULL; collectedImg = NULL;
    for (int i = 0; i < entityCount; i++) {
        if (entityPool[i].baseImage) CP_Image_Free(entityPool[i].baseImage);
        if (entityPool[i].altImage) CP_Image_Free(entityPool[i].altImage);
        if (entityPool[i].idleImg) CP_Image_Free(entityPool[i].idleImg);
        if (entityPool[i].runImg) CP_Image_Free(entityPool[i].runImg);
        if (entityPool[i].hitImg) CP_Image_Free(entityPool[i].hitImg);
        entityPool[i].baseImage = NULL; entityPool[i].altImage = NULL;
        entityPool[i].idleImg = NULL; entityPool[i].runImg = NULL;
        entityPool[i].hitImg = NULL;
    }
    entityCount = 0;  // spawn pool restarts empty on next entry
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
    playerHitSnd = NULL; hitSnd = NULL; deathSnd = NULL; coinSnd = NULL;
    menuMusic = NULL; worldMusic = NULL; stepSnd = NULL; jumpSnd = NULL;
    respawnSnd = NULL; clickSnd = NULL; windowOpenSnd = NULL;
    if (mapData) free(mapData);
    mapData = NULL; mapWidth = 0; mapHeight = 0;
}