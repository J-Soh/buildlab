// memory leak checker
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include "cprocessing.h"
#include "utils.h"
#include "menu.h"
#include "game.h"
#include "skin.h"  // skin picker overlay + selected-skin portrait button
// --- Menu button art (shared with game_exit, which frees them at terminate) ---
CP_Image playBtnImage = NULL;
CP_Image volBtnImage = NULL;
CP_Image creditBtnImage = NULL;
CP_Image digipenLogoImage = NULL;  // DigiPen logo shown on the credits page
CP_Image buildlabLogoImage = NULL;  // team logo shown on the splash screen

// --- Splash screens (exe start only): DigiPen logo 2s, then team logo 2s ---
#define SPLASH_TIME 2.0f  // seconds each splash screen stays up (skippable)
static int splashStage = 0;      // 0 DigiPen, 1 team, 2 done (main menu)
static float splashT = 0.0f;
static bool splashShown = false; // splash plays once per exe run, not on quit-to-menu returns

// Menu page state
static bool showSettings = false; // Toggle to view instructions overlay
static bool showCredits = false; // Toggle to view credits page (info from credit.md)
static bool showSkins = false;   // Toggle to view the character skin picker
static bool showQuitConfirm = false; // Quit popup on the main menu (ESC -> "Are you sure...?")


// Forward declarations (defined below menu_update)
static void draw_menu_ui(void);
static void handle_menu_input(void);
static void update_splash(void);  // splash timer + skip, then main menu
static void draw_splash(void);

void menu_init(void) {
    CP_System_SetWindowSize((int)screenW, (int)screenH);
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // Set refresh rate for current computer / monitor's max refresh rate.
    int refresh = CP_System_GetDisplayRefreshRate();
    if (refresh > 0) CP_System_SetFrameRate((float)refresh);

    CP_System_SetWindowTitle("Pixel Adventure");

    CP_Settings_ImageMode(CP_POSITION_CORNER);
    CP_Settings_ImageFilterMode(CP_IMAGE_FILTER_NEAREST); // make image more sharper

    // Menu + shared assets (shared ones are freed in game_exit, not menu_exit,
    // because the menu can be revisited — menu_exit must free nothing).
    // settingsBtnImage lives in utils.c (the in-game options button reuses it).
    if (!bgImage) bgImage = CP_Image_Load("Assets/Background/Brown.png");
    if (!gameFont) gameFont = CP_Font_Load("Assets/ThaleahFat.ttf");
    if (!playBtnImage) playBtnImage = CP_Image_Load("Assets/Menu/Buttons/Play.png");
    if (!volBtnImage) volBtnImage = CP_Image_Load("Assets/Menu/Buttons/Volume.png");
    if (!creditBtnImage) creditBtnImage = CP_Image_Load("Assets/Menu/Buttons/credit.png");
    if (!digipenLogoImage) digipenLogoImage = CP_Image_Load("Assets/Other/digipen.png");
    if (!buildlabLogoImage) buildlabLogoImage = CP_Image_Load("Assets/Other/buildlab.png");
    if (!settingsBtnImage) settingsBtnImage = CP_Image_Load("Assets/Menu/Buttons/Settings.png");
    if (!clickSnd) clickSnd = CP_Sound_Load("Assets/Sound/button_click.wav");
    if (!windowOpenSnd) windowOpenSnd = CP_Sound_Load("Assets/Sound/window_open.wav");
    if (!menuMusic) menuMusic = CP_Sound_LoadMusic("Assets/Sound/menu_music.wav");
    skin_init_assets();  // idle portraits for the skin button + picker
    apply_volumes(); // defined in utils.c

    // Clean page state on every entry (including quit-to-menu returns).
    // Splash restarts only on first exe run — returns to menu skip it.
    if (!splashShown) { splashStage = 0; splashT = 0.0f; }
    showSettings = false;
    showCredits = false;
    showSkins = false;
    showQuitConfirm = false;
}

void menu_update(void) {
    // Keep the logical screen in sync with the fullscreen (or resized) window
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // Splash screens first (exe start only); main menu after stage 2
    if (splashStage < 2) { update_splash(); return; }

    // Clear screen and draw menu background (with animation going downwards)
    CP_Graphics_ClearBackground(CP_Color_Create(0, 0, 0, 255));
    // CP_System_GetDt()= dt = 1 / 120 (current pc fps) = 0.00833
    bgScroll += BG_SCROLL_SPEED * CP_System_GetDt(); // from utils.h (BG_SCROLL_SPEED = 70.0f)
    
    // Play music (run once)
    // if current music is not menu music and menuMusic is loaded successfully, execute below
    if (musicMode != 1 && menuMusic) {
        // Format: CP_Sound_StopGroup(CP_SOUND_GROUP_SFX) will stop sound related when defined in CP_Sound_PlayAdvanced(jumpSnd,.., CP_SOUND_GROUP_SFX);
        // Format: CP_Sound_StopGroup(CP_SOUND_GROUP_MUSIC) will stop all sound whose defined in CP_Sound_PlayAdvanced(worldMusic, ..., CP_SOUND_GROUP_MUSIC); or CP_Sound_PlayAdvanced(menuMusic, ..., CP_SOUND_GROUP_MUSIC);
        // Stop all music sound being played
        CP_Sound_StopGroup(CP_SOUND_GROUP_MUSIC); 
        // Format: CP_Sound_PlayAdvanced( sound, volume, pitch, loop, sound group );
        CP_Sound_PlayAdvanced(menuMusic, musicVol, 1.0f, (CP_BOOL)1, CP_SOUND_GROUP_MUSIC);
        musicMode = 1;
    }

    draw_background(); // defined in utils.c

    // Skin picker takes over the screen while open (it draws its own panel).
    if (showSkins) {
        skin_picker_draw();
        if (CP_Input_KeyTriggered(KEY_ESCAPE)) {  // ESC also backs out
            showSkins = false;
            play_click();
            return;
        }
        if (skin_picker_handle_input()) showSkins = false;  // BACK clicked
        return;
    }

    draw_menu_ui();
    handle_menu_input();
}

// Splash timer + skip: each stage lasts SPLASH_TIME seconds, any key or
// click jumps early to the next screen. Timer-based so a missing image can
// splashStage = 0 (Digipen Logo), then increase by 1 (Buildlab logo), then increase again (2 == go main menu)
static void update_splash(void) {
    splashT += CP_System_GetDt();
    if (CP_Input_MouseClicked() || CP_Input_KeyTriggered(KEY_SPACE)
        || CP_Input_KeyTriggered(KEY_ENTER) || CP_Input_KeyTriggered(KEY_ESCAPE)) {
        splashT = 0.0f;
        splashStage++;
    }
    else if (splashT >= SPLASH_TIME) {
        splashT = 0.0f;
        splashStage++;
    }
    // if digipen logo and buildlab logo finish showing, then it enters main menu.
    if (splashStage >= 2) { splashShown = true; return; }
    draw_splash();
}

// DigiPen logo Splash screen
static void draw_splash(void) {
    CP_Graphics_ClearBackground(CP_Color_Create(0, 0, 0, 255));

    float centerX = SCREEN_WIDTH * 0.5f;
    CP_Image logo = (splashStage == 0) ? digipenLogoImage : buildlabLogoImage;
    if (logo) {
        float lw = (float)CP_Image_GetWidth(logo);
        float lh = (float)CP_Image_GetHeight(logo);
        // Per-stage height: DigiPen 220px is perfect; the team logo's art is
        // smaller, so it draws at 330px. Aspect is still kept either way.
        float lhDraw = (splashStage == 0) ? 220.0f : 330.0f;
        float lwDraw = (lh > 0.0f) ? lhDraw * lw / lh : 440.0f;
        if (lwDraw > SCREEN_WIDTH * 0.8f) {  // very wide logo: fit width instead
            lwDraw = SCREEN_WIDTH * 0.8f;
            lhDraw = (lw > 0.0f) ? lwDraw * lh / lw : 110.0f;
        }
        CP_Image_Draw(logo, centerX - lwDraw * 0.5f, 330.0f - lhDraw * 0.5f, lwDraw, lhDraw, 255);
    }

    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
    if (splashStage == 0) {
        CP_Settings_Fill(CP_Color_Create(200, 200, 200, 255));
        CP_Settings_TextSize(30.0f);
        CP_Font_DrawText("All content (c) 2026 DigiPen Institute of Technology Singapore.", centerX, 560.0f);
        CP_Font_DrawText("All Rights Reserved.", centerX, 600.0f);
    }
    CP_Settings_Fill(CP_Color_Create(120, 120, 120, 255));
    CP_Settings_TextSize(24.0f);
    CP_Font_DrawText("Press any key to skip", centerX, SCREEN_HEIGHT - 60.0f);
}

void menu_exit(void) {
    // Intentionally frees nothing: shared assets (font, sounds) belong to the
    // game state, and button art is freed inline in game_exit at terminate.
}

// Draws the Title Screen UI
static void draw_quit_confirm(void);  // defined after draw_menu_ui; fwd so the menu can overlay it
static void draw_menu_ui(void) {
    float centerX = SCREEN_WIDTH * 0.5f;
    float centerY = SCREEN_HEIGHT * 0.5f;

    // Responsive card: ~62% of width, ~70% of height, centered on screen
    float panelW = SCREEN_WIDTH * 0.62f;
    float panelH = SCREEN_HEIGHT * 0.70f;
    float panelTop = centerY - panelH * 0.5f;

    // Format: (x-axis, y-axis, width, height)
    draw_cream_panel(centerX, centerY, panelW, panelH); // defined in utils.c

    if (gameFont) { // activate font
        CP_Font_Set(gameFont);
    }

    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    if (showSettings) {
        // --- SETTINGS OVERLAY ---
        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255)); // dark reddish-brown shade
        CP_Settings_TextSize(panelH * 0.108f);
        // CP_Font_DrawText(" text ", x-axis, y-axis)
        CP_Font_DrawText("CONTROLS & SETTINGS", centerX, panelTop + panelH * 0.22f);

        CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255)); // dark gray shade
        CP_Settings_TextSize(panelH * 0.064f);
        CP_Font_DrawText("A / D  -  Move Left / Right", centerX, panelTop + panelH * 0.43f);
        CP_Font_DrawText("SPACE  -  Jump / Double Jump", centerX, panelTop + panelH * 0.55f);

        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
        CP_Settings_TextSize(panelH * 0.056f);
        CP_Font_DrawText("Click anywhere on card to go BACK", centerX, panelTop + panelH * 0.73f);
    }
    else if (showCredits) {
        // --- CREDITS PAGE (info from credit.md in project root) ---
        if (digipenLogoImage) {  
            float lw = (float)CP_Image_GetWidth(digipenLogoImage);
            float lh = (float)CP_Image_GetHeight(digipenLogoImage);
            float lhDraw = 50.0f, lwDraw = (lh > 0.0f) ? 50.0f * lw / lh : 150.0f;
            CP_Image_Draw(digipenLogoImage, centerX - lwDraw * 0.5f, panelTop + panelH * 0.05f, lwDraw, lhDraw, 255);
        }

        CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
        CP_Settings_TextSize(panelH * 0.048f);
        CP_Font_DrawText("All content (c) 2026 DigiPen Institute of Technology Singapore.", centerX, panelTop + panelH * 0.2f);
        CP_Font_DrawText("All Rights Reserved.", centerX, panelTop + panelH * 0.25f);
        CP_Font_DrawText("Team: Joshua Soh, A K Ahmed Arshad,", centerX, panelTop + panelH * 0.35f);
        CP_Font_DrawText("Eoghan Loke, Tran Cong Vinh", centerX, panelTop + panelH * 0.4f);
        CP_Font_DrawText("Instructors: Gerald Wong, Dr. Soroor, Tommy Tan", centerX, panelTop + panelH * 0.5f);
        CP_Font_DrawText("President: Claude Comair", centerX, panelTop + panelH * 0.63f);
        CP_Font_DrawText("Executives: CHU Jason Yeu Tat, Michael GATS, TAN Chek Ming, ", centerX, panelTop + panelH * 0.75f);
        CP_Font_DrawText("Prasanna Kumar GHALI, Mandy WONG, Johnny DEEK", centerX, panelTop + panelH * 0.8f);
        
   
        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
        CP_Settings_TextSize(panelH * 0.056f);
        CP_Font_DrawText("Click anywhere on card to go BACK", centerX, panelTop + panelH * 0.95f);
    }
    else {
        // --- MAIN MENU OVERLAY ---
        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
        CP_Settings_TextSize(panelH * 0.144f);
        CP_Font_DrawText("PIXEL ADVENTURE", centerX, panelTop + panelH * 0.22f);

        CP_Settings_Fill(CP_Color_Create(80, 80, 80, 255));
        CP_Settings_TextSize(panelH * 0.056f);
        CP_Font_DrawText("Press [ SPACE ] or click PLAY button to Start", centerX, panelTop + panelH * 0.36f);

        CP_Settings_Fill(CP_Color_Create(120, 120, 120, 255));
        CP_Settings_TextSize(panelH * 0.044f);
        CP_Font_DrawText("Press [ ESC ] to Quit", centerX, panelTop + panelH * 0.44f);

        float btnSize = panelH * 0.128f;
        float btnY = panelTop + panelH * 0.58f;
        float btnOffset = panelW * 0.125f;
        // Label text sits centered below each icon (alignment is H_CENTER/V_MIDDLE above)
        float labelY = btnY + btnSize + panelH * 0.035f;

        CP_Settings_Fill(CP_Color_Create(80, 80, 80, 255));
        CP_Settings_TextSize(panelH * 0.044f);

        if (playBtnImage) {
            CP_Image_Draw(playBtnImage, centerX - (btnSize * 0.5f), btnY, btnSize, btnSize, 255);
            CP_Font_DrawText("Play", centerX, labelY); // text below the play icon
        }

        if (volBtnImage) {
            float vx = centerX - (btnSize * 0.5f) - btnOffset;
            CP_Image_Draw(volBtnImage, vx, btnY, btnSize, btnSize, muted ? 140 : 255);
            CP_Font_DrawText("Vol", vx + btnSize * 0.5f, labelY); // text below the volume icon
            if (muted) {  // red X = muted
                float inset = btnSize * 0.125f;
                CP_Settings_Stroke(CP_Color_Create(180, 30, 25, 255));
                CP_Settings_StrokeWeight(panelH * 0.014f);
                CP_Graphics_DrawLine(vx + inset, btnY + inset, vx + btnSize - inset, btnY + btnSize - inset);
                CP_Graphics_DrawLine(vx + btnSize - inset, btnY + inset, vx + inset, btnY + btnSize - inset);
                CP_Settings_NoStroke();
            }
        }

        // Second row: CREDIT + SKIN picker, side by side under the first row.
        float row2Y = btnY + 130.0f;
        float row2LabelY = labelY + 130.0f;
        float creditX = centerX - (btnSize * 0.5f) - btnOffset * 0.5f;
        float skinX = centerX - (btnSize * 0.5f) + btnOffset * 0.5f;

        if (creditBtnImage) {
            CP_Image_Draw(creditBtnImage, creditX, row2Y, btnSize, btnSize, 255);
            CP_Font_DrawText("Credit", creditX + btnSize * 0.5f, row2LabelY); // text below the credit icon
        }

        // Skin button: portrait of the currently selected character, so the
        // player can see the active skin before opening the picker.
        {
            bool skinHover = is_mouse_over(skinX, row2Y, btnSize, btnSize);
            skin_draw_preview(skinIndex, skinX, row2Y, btnSize);
            if (skinHover) {
                CP_Settings_NoFill();
                CP_Settings_Stroke(CP_Color_Create(215, 140, 40, 255));
                CP_Settings_StrokeWeight(4.0f);
                CP_Graphics_DrawRect(skinX - 4.0f, row2Y - 4.0f, btnSize + 8.0f, btnSize + 8.0f);
                CP_Settings_NoStroke();
                CP_Settings_Fill(CP_Color_Create(80, 80, 80, 255));
            }
            CP_Font_DrawText("Skin", skinX + btnSize * 0.5f, row2LabelY); // text below the portrait
        }

        if (settingsBtnImage) {
            float sx = centerX - (btnSize * 0.5f) + btnOffset;
            CP_Image_Draw(settingsBtnImage, sx, btnY, btnSize, btnSize, 255);
            CP_Font_DrawText("Controls", sx + btnSize * 0.5f, labelY); // text below the settings icon
        }
    }

    draw_quit_confirm();  // ESC popup overlays everything on the menu (no-op unless open)
}

// Quit-confirm popup (main menu only): dim + cream card + YES / NO.
static void draw_quit_confirm(void) {
    if (!showQuitConfirm) return;

    // Dark overlay over the menu (same look as the pause menu dim)
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Card
    // Format: (x-axis, y-axis, width, height)
    draw_cream_panel(SCREEN_WIDTH * 0.5f, 360.0f, 560.0f, 250.0f);
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(52.0f);
    CP_Font_DrawText("QUIT GAME?", SCREEN_WIDTH * 0.5f, 300.0f);

    CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255)); // Dark charcoal gray text color
    CP_Settings_TextSize(30.0f);
    CP_Font_DrawText("Are you sure you want to quit?", SCREEN_WIDTH * 0.5f, 348.0f);

    // YES / NO buttons (hover highlight + label, pause-menu style)
    CP_Settings_RectMode(CP_POSITION_CORNER);
    // Format: is_mouse_over(x-axis, y-axis, rect width, rect height)
    // Check if mouse is over "YES" or "NO" button for "QUIT menu" card.
    if (is_mouse_over(580.0f, 404.0f, 160.0f, 52.0f)) { // "Yes" button
        // draw orange near-transparent color rect when user mouse hovers over it
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(580.0f, 404.0f, 160.0f, 52.0f);
    }
    if (is_mouse_over(860.0f, 404.0f, 160.0f, 52.0f)) { // "NO" Button
        // draw orange near-transparent color rect when user mouse hovers over it
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(860.0f, 404.0f, 160.0f, 52.0f);
    }

    CP_Settings_TextSize(38.0f);
    // Change Text "Yes" or "No" color from dark charcoal gray to Orange if mouse hover over one of them. Else, they stayed dark charcoal gray.
    CP_Settings_Fill(is_mouse_over(580.0f, 404.0f, 160.0f, 52.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("YES", (1320.0f * 0.5f), 430);
    CP_Settings_Fill(is_mouse_over(860.0f, 404.0f, 160.0f, 52.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("NO", (1880 * 0.5f), 430);
}

// Handles input during the menu screen (PLAY/SPACE enters the game immediately)
static void handle_menu_input(void) {
    // ESC opens the quit popup (main page only); ESC again closes it.
    // Y / ENTER confirms quit, N cancels. Popup blocks SPACE-start below.
    if (CP_Input_KeyTriggered(KEY_ESCAPE)) {
        if (showQuitConfirm) showQuitConfirm = false;
        else if (!showSettings && !showCredits) {
            showQuitConfirm = true;
            if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
        }
    }
    if (showQuitConfirm) {
        if (CP_Input_KeyTriggered(KEY_Y) || CP_Input_KeyTriggered(KEY_ENTER)) {
            CP_Engine_Terminate();  // YES: quit the game
            return;
        }
        if (CP_Input_KeyTriggered(KEY_N)) { showQuitConfirm = false; return; }
    }

    if (CP_Input_KeyTriggered(KEY_SPACE) && !showQuitConfirm) {
        CP_Engine_SetNextGameState(game_init, game_update, game_exit);  // PLAY: enter game
    }

    if (CP_Input_MouseClicked()) {
        // Popup open: only YES / NO count; menu buttons underneath are blocked.
        if (showQuitConfirm) {
            if (is_mouse_over(580.0f, 404.0f, 160.0f, 52.0f)) {
                CP_Engine_Terminate();  // YES: quit the game
            }
            else if (is_mouse_over(860.0f, 404.0f, 160.0f, 52.0f)) {
                showQuitConfirm = false;  // NO: back to menu
                play_click();
            }
            return;
        }
        float centerX = SCREEN_WIDTH * 0.5f;
        float panelW = SCREEN_WIDTH * 0.62f;
        float panelH = SCREEN_HEIGHT * 0.70f;
        float panelTop = SCREEN_HEIGHT * 0.5f - panelH * 0.5f;

        if (showSettings || showCredits) {
            if (is_mouse_over(centerX - (panelW * 0.5f), panelTop, panelW, panelH)) {
                showSettings = false;
                showCredits = false;
                play_click();
            }
        }
        else {
            float btnSize = panelH * 0.128f;
            float btnY = panelTop + panelH * 0.58f;
            float btnOffset = panelW * 0.125f;

            float playX = centerX - (btnSize * 0.5f);
            float volX = playX - btnOffset;
            // Second row is CREDIT + SKIN side by side (see draw_menu_ui);
            // keep these boxes identical to the draw calls.
            float row2Y = btnY + 130.0f;
            float creditX = centerX - (btnSize * 0.5f) - btnOffset * 0.5f;
            float skinX = centerX - (btnSize * 0.5f) + btnOffset * 0.5f;
            float settingsX = playX + btnOffset;

            if (is_mouse_over(playX, btnY, btnSize, btnSize)) {
                play_click();
                CP_Engine_SetNextGameState(game_init, game_update, game_exit);  // PLAY: enter game
            }

            // Volume button: toggles mute (red X shows muted)
            if (is_mouse_over(volX, btnY, btnSize, btnSize)) {
                muted = !muted;
                apply_volumes();
                play_click();
            }

            if (is_mouse_over(settingsX, btnY, btnSize, btnSize)) {
                showSettings = true;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                play_click();
            }

            if (is_mouse_over(creditX, row2Y, btnSize, btnSize)) {
                showCredits = true;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                play_click();
            }

            // Skin button: opens the character picker (default blue astronaut).
            if (is_mouse_over(skinX, row2Y, btnSize, btnSize)) {
                showSkins = true;
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                play_click();
            }
        }
    }
}
