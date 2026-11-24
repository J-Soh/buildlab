// memory leak checker
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <math.h>

#include "cprocessing.h"
#include "utils.h"
#include "menu.h"
#include "game.h"
#include "skin.h"  // skin picker overlay + selected-skin portrait button
// --- Menu button art (shared with game_exit, which frees them at terminate) ---
CP_Image playBtnImage = NULL;
CP_Image volBtnImage = NULL;

// Menu page state
static bool showSettings = false; // Toggle to view instructions overlay
static bool showSkins = false;   // Toggle to view the character skin picker
static bool showQuitConfirm = false; // Quit popup on the main menu (ESC -> "Are you sure...?")

typedef enum {
    MENU_TRANSITION_NONE = 0,
    MENU_TRANSITION_GAME,
    MENU_TRANSITION_SETTINGS_OPEN,
    MENU_TRANSITION_SETTINGS_CLOSE,
    MENU_TRANSITION_SKINS_OPEN,
    MENU_TRANSITION_SKINS_CLOSE
} MenuTransitionAction;

#define MENU_TRANSITION_DURATION 0.36f
static int menuTransitionPhase = 0; // 0 idle, 1 fading out, 2 fading in
static float menuTransitionT = 0.0f;
static MenuTransitionAction menuTransitionAction = MENU_TRANSITION_NONE;

static void start_menu_transition(MenuTransitionAction action) {
    if (menuTransitionPhase != 0) return;
    menuTransitionAction = action;
    menuTransitionT = 0.0f;
    menuTransitionPhase = 1;
}

static void update_menu_transition(float dt) {
    if (menuTransitionPhase == 0) return;

    menuTransitionT += dt;
    if (menuTransitionPhase == 1 && menuTransitionT >= MENU_TRANSITION_DURATION) {
        switch (menuTransitionAction) {
            case MENU_TRANSITION_GAME:
                CP_Engine_SetNextGameState(game_init, game_update, game_exit);
                return;
            case MENU_TRANSITION_SETTINGS_OPEN:
                showSettings = true;
                break;
            case MENU_TRANSITION_SETTINGS_CLOSE:
                showSettings = false;
                break;
            case MENU_TRANSITION_SKINS_OPEN:
                showSkins = true;
                break;
            case MENU_TRANSITION_SKINS_CLOSE:
                showSkins = false;
                break;
            default:
                break;
        }
        menuTransitionAction = MENU_TRANSITION_NONE;
        menuTransitionT = 0.0f;
        menuTransitionPhase = 2;
    } else if (menuTransitionPhase == 2 && menuTransitionT >= MENU_TRANSITION_DURATION) {
        menuTransitionPhase = 0;
        menuTransitionT = 0.0f;
    }
}

static void draw_menu_transition(void) {
    if (menuTransitionPhase == 0) return;

    float progress;
    if (menuTransitionPhase == 1) {
        progress = menuTransitionT / MENU_TRANSITION_DURATION;
    } else {
        progress = 1.0f - menuTransitionT / MENU_TRANSITION_DURATION;
    }
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    if (!transitionImage) return;
    CP_Settings_ImageMode(CP_POSITION_CORNER);
    float cx = SCREEN_WIDTH * 0.5f;
    float cy = SCREEN_HEIGHT * 0.5f;
    float coverSize = 2.0f * sqrtf(cx * cx + cy * cy);
    float startSize = 44.0f;
    float size = startSize + (coverSize - startSize) * progress;
    CP_Image_Draw(transitionImage,
                  cx - size * 0.5f,
                  cy - size * 0.5f,
                  size,
                  size,
                  255);
}

// Forward declarations (defined below menu_update)
static void draw_menu_ui(void);
static void handle_menu_input(void);

void menu_init(void) {
    CP_System_Fullscreen();
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

    // Keep the frame limiter from capping the game at the monitor's refresh rate.
    CP_System_SetFrameRate(1000.0f);

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
    if (!transitionImage) transitionImage = CP_Image_Load("Assets/Other/Transition.png");
    if (!settingsBtnImage) settingsBtnImage = CP_Image_Load("Assets/Menu/Buttons/Settings.png");
    if (!clickSnd) clickSnd = CP_Sound_Load("Assets/Sound/button_click.wav");
    if (!windowOpenSnd) windowOpenSnd = CP_Sound_Load("Assets/Sound/window_open.wav");
    if (!menuMusic) menuMusic = CP_Sound_LoadMusic("Assets/Sound/menu_music.wav");
    skin_init_assets();  // idle portraits for the skin button + picker
    apply_volumes(); // defined in utils.c

    // Clean page state on every entry (including quit-to-menu returns).
    showSettings = false;
    showSkins = false;
    showQuitConfirm = false;
    menuTransitionPhase = 2;
    menuTransitionT = 0.0f;
    menuTransitionAction = MENU_TRANSITION_NONE;
}

void menu_update(void) {
    // Keep the logical screen in sync with the fullscreen (or resized) window
    screenW = (float)CP_System_GetWindowWidth();
    screenH = (float)CP_System_GetWindowHeight();

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

    update_menu_transition(CP_System_GetDt());

    // Skin picker takes over the screen while open (it draws its own panel).
    if (showSkins && menuTransitionPhase == 0) {
        skin_picker_draw();
        if (CP_Input_KeyTriggered(KEY_ESCAPE)) {  // ESC also backs out
            start_menu_transition(MENU_TRANSITION_SKINS_CLOSE);
            play_click();
            return;
        }
        if (skin_picker_handle_input()) {
            start_menu_transition(MENU_TRANSITION_SKINS_CLOSE);
        }
        return;
    }

    draw_menu_ui();
    if (menuTransitionPhase == 0) handle_menu_input();
    draw_menu_transition();
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

        // Skin picker is centered on the second row.
        float row2Y = btnY + 130.0f;
        float row2LabelY = labelY + 130.0f;
        float skinX = centerX - btnSize * 0.5f;

        // Skin button: portrait of the currently selected character, so the
        // player can see the active skin before opening the picker.
        {
            bool skinHover = is_mouse_over(skinX, row2Y, btnSize, btnSize);
            skin_draw_preview(skinIndex, skinX, row2Y, btnSize, 0);
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
    float centerX = SCREEN_WIDTH * 0.5f;
    float yesX = centerX - 220.0f;
    float noX = centerX + 60.0f;

    // Dark overlay over the menu (same look as the pause menu dim)
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Card
    // Format: (x-axis, y-axis, width, height)
    draw_cream_panel(centerX, 360.0f, 560.0f, 250.0f);
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(52.0f);
    CP_Font_DrawText("QUIT GAME?", centerX, 300.0f);

    CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255)); // Dark charcoal gray text color
    CP_Settings_TextSize(30.0f);
    CP_Font_DrawText("Are you sure you want to quit?", centerX, 348.0f);

    // YES / NO buttons (hover highlight + label, pause-menu style)
    CP_Settings_RectMode(CP_POSITION_CORNER);
    // Format: is_mouse_over(x-axis, y-axis, rect width, rect height)
    // Check if mouse is over "YES" or "NO" button for "QUIT menu" card.
    if (is_mouse_over(yesX, 404.0f, 160.0f, 52.0f)) { // "Yes" button
        // draw orange near-transparent color rect when user mouse hovers over it
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(yesX, 404.0f, 160.0f, 52.0f);
    }
    if (is_mouse_over(noX, 404.0f, 160.0f, 52.0f)) { // "NO" Button
        // draw orange near-transparent color rect when user mouse hovers over it
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(noX, 404.0f, 160.0f, 52.0f);
    }

    CP_Settings_TextSize(38.0f);
    // Change Text "Yes" or "No" color from dark charcoal gray to Orange if mouse hover over one of them. Else, they stayed dark charcoal gray.
    CP_Settings_Fill(is_mouse_over(yesX, 404.0f, 160.0f, 52.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("YES", yesX + 80.0f, 430);
    CP_Settings_Fill(is_mouse_over(noX, 404.0f, 160.0f, 52.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("NO", noX + 80.0f, 430);
}

// Handles input during the menu screen (PLAY/SPACE enters the game immediately)
static void handle_menu_input(void) {
    // ESC opens the quit popup (main page only); ESC again closes it.
    // Y / ENTER confirms quit, N cancels. Popup blocks SPACE-start below.
    if (CP_Input_KeyTriggered(KEY_ESCAPE)) {
        if (showQuitConfirm) showQuitConfirm = false;
        else if (!showSettings) {
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

    if (CP_Input_KeyTriggered(KEY_SPACE) &&
        !showQuitConfirm && !showSettings && !showSkins) {
        play_click();
        start_menu_transition(MENU_TRANSITION_GAME);
    }

    if (CP_Input_MouseClicked()) {
        // Popup open: only YES / NO count; menu buttons underneath are blocked.
        if (showQuitConfirm) {
            float centerX = SCREEN_WIDTH * 0.5f;
            if (is_mouse_over(centerX - 220.0f, 404.0f, 160.0f, 52.0f)) {
                CP_Engine_Terminate();  // YES: quit the game
            }
            else if (is_mouse_over(centerX + 60.0f, 404.0f, 160.0f, 52.0f)) {
                showQuitConfirm = false;  // NO: back to menu
                play_click();
            }
            return;
        }
        float centerX = SCREEN_WIDTH * 0.5f;
        float panelW = SCREEN_WIDTH * 0.62f;
        float panelH = SCREEN_HEIGHT * 0.70f;
        float panelTop = SCREEN_HEIGHT * 0.5f - panelH * 0.5f;

        if (showSettings) {
            if (is_mouse_over(centerX - (panelW * 0.5f), panelTop, panelW, panelH)) {
                play_click();
                if (showSettings) start_menu_transition(MENU_TRANSITION_SETTINGS_CLOSE);
            }
        }
        else {
            float btnSize = panelH * 0.128f;
            float btnY = panelTop + panelH * 0.58f;
            float btnOffset = panelW * 0.125f;

            float playX = centerX - (btnSize * 0.5f);
            float volX = playX - btnOffset;
            // Match the centered skin button in draw_menu_ui.
            float row2Y = btnY + 130.0f;
            float skinX = centerX - btnSize * 0.5f;
            float settingsX = playX + btnOffset;

            if (is_mouse_over(playX, btnY, btnSize, btnSize)) {
                play_click();
                start_menu_transition(MENU_TRANSITION_GAME);
            }

            // Volume button: toggles mute (red X shows muted)
            if (is_mouse_over(volX, btnY, btnSize, btnSize)) {
                muted = !muted;
                apply_volumes();
                play_click();
            }

            if (is_mouse_over(settingsX, btnY, btnSize, btnSize)) {
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                play_click();
                start_menu_transition(MENU_TRANSITION_SETTINGS_OPEN);
            }

            // Skin button: opens the character picker (default blue astronaut).
            if (is_mouse_over(skinX, row2Y, btnSize, btnSize)) {
                if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
                play_click();
                start_menu_transition(MENU_TRANSITION_SKINS_OPEN);
            }
        }
    }
}
