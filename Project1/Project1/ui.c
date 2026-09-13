// ui.c
// Title screen, main menu, pause menu, quit confirmation popup, and diamond transition.
#include "ui.h"
#include "audio.h"
#include "player.h"
#include "entity.h"
#include "map.h"

// ---- UI state ----
bool showSettings = false;
bool showQuitConfirm = false;

// ---- Image & Font handles ----
CP_Image bgImage = NULL;
CP_Font  gameFont = NULL;
CP_Image playBtnImage = NULL;
CP_Image volBtnImage = NULL;
CP_Image settingsBtnImage = NULL;
CP_Image restartBtnImage = NULL;

// ---- Pause menu state ----
bool paused = false;
int  pausePage = 0;

// ---- Transition state ----
CP_Image transitionImage = NULL;
bool     isTransitioning = false;
float    transitionTimer = 0.0f;

// ---- Ambient background scroll ----
float bgScroll = 0.0f;

// Checks if mouse position is inside an image bounding box
bool is_mouse_over(float x, float y, float width, float height) {
    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();
    return (mx >= x && mx <= x + width && my >= y && my <= y + height);
}

// Brown image across the screen, rising continuously and wrapped edge-to-edge
void draw_background(void) {
    if (!bgImage) return;

    float bgWidth = (float)CP_Image_GetWidth(bgImage);
    float bgHeight = (float)CP_Image_GetHeight(bgImage);
    if (bgWidth <= 0.0f || bgHeight <= 0.0f) return;

    float offY = floorf(fmodf(bgScroll, bgHeight));
    for (float y = offY - bgHeight; y < screenH; y += bgHeight) {
        for (float x = 0.0f; x < screenW; x += bgWidth) {
            CP_Image_Draw(bgImage, x, y, bgWidth + BG_CRACK_SEAL, bgHeight + BG_CRACK_SEAL, 255);
        }
    }
}

// Renders the scaling transition over screen when going from menu to gameplay (In & Out)
void draw_transition(void) {
    if (!isTransitioning || !transitionImage) return;

    transitionTimer += CP_System_GetDt();
    float progress = transitionTimer / TRANSITION_DURATION;

    // Phase 1: Expanding outward to cover the screen
    if (progress <= 0.5f) {
        float expandProgress = progress * 2.0f;
        float maxScale = sqrtf(screenW * screenW + screenH * screenH);
        float currentSize = expandProgress * maxScale;

        CP_Settings_ImageMode(CP_POSITION_CENTER);
        CP_Image_Draw(transitionImage, screenW * 0.5f, screenH * 0.5f, currentSize, currentSize, 255);
        CP_Settings_ImageMode(CP_POSITION_CORNER);
    }
    // Phase 2: Switch to GAME state and shrink diamond back to center
    else if (progress < 1.0f) {
        currentState = STATE_GAME;

        float shrinkProgress = 1.0f - ((progress - 0.5f) * 2.0f);
        float maxScale = sqrtf(screenW * screenW + screenH * screenH);
        float currentSize = shrinkProgress * maxScale;

        CP_Settings_ImageMode(CP_POSITION_CENTER);
        CP_Image_Draw(transitionImage, screenW * 0.5f, screenH * 0.5f, currentSize, currentSize, 255);
        CP_Settings_ImageMode(CP_POSITION_CORNER);
    }
    // Transition complete
    else {
        isTransitioning = false;
        transitionTimer = 0.0f;
    }
}

// Cream panel helper (original menu card style: shadow + cream + gold border)
void draw_cream_panel(float cx, float cy, float w, float h) {
    CP_Settings_RectMode(CP_POSITION_CENTER);
    CP_Settings_Fill(CP_Color_Create(40, 20, 50, 220));
    CP_Graphics_DrawRect(cx, cy, w + SX(16.0f), h + SY(16.0f));
    CP_Settings_Fill(CP_Color_Create(245, 230, 210, 240));
    CP_Graphics_DrawRect(cx, cy, w, h);
    CP_Settings_NoFill();
    CP_Settings_Stroke(CP_Color_Create(215, 140, 40, 255));
    CP_Settings_StrokeWeight(SF(6.0f));
    CP_Graphics_DrawRect(cx, cy, w - SX(24.0f), h - SY(24.0f));
    CP_Settings_NoStroke();
    CP_Settings_RectMode(CP_POSITION_CORNER);
}

// Quit-confirm popup (main menu only): dim + cream card + YES / NO.
static float quit_yes_x0(void) { return SX(400.0f); }
static float quit_yes_x1(void) { return SX(560.0f); }
static float quit_no_x0(void) { return SX(680.0f); }
static float quit_no_x1(void) { return SX(840.0f); }
static float quit_btn_y0(void) { return SY(404.0f); }
static float quit_btn_y1(void) { return SY(456.0f); }
static float quit_btn_yc(void) { return SY(430.0f); }

static void draw_quit_confirm(void) {
    if (!showQuitConfirm) return;

    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, screenW, screenH);

    draw_cream_panel(screenW * 0.5f, SY(360.0f), SX(560.0f), SY(250.0f));
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(SF(52.0f));
    CP_Font_DrawText("QUIT GAME?", screenW * 0.5f, SY(300.0f));

    CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
    CP_Settings_TextSize(SF(30.0f));
    CP_Font_DrawText("Are you sure you want to quit?", screenW * 0.5f, SY(348.0f));

    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();
    bool hovYes = mx >= quit_yes_x0() && mx <= quit_yes_x1() && my >= quit_btn_y0() && my <= quit_btn_y1();
    bool hovNo = mx >= quit_no_x0() && mx <= quit_no_x1() && my >= quit_btn_y0() && my <= quit_btn_y1();

    CP_Settings_RectMode(CP_POSITION_CORNER);
    if (hovYes) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(quit_yes_x0(), quit_btn_y0(), quit_yes_x1() - quit_yes_x0(), quit_btn_y1() - quit_btn_y0());
    }
    if (hovNo) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(quit_no_x0(), quit_btn_y0(), quit_no_x1() - quit_no_x0(), quit_btn_y1() - quit_btn_y0());
    }
    CP_Settings_TextSize(SF(36.0f));
    CP_Settings_Fill(hovYes ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("YES", (quit_yes_x0() + quit_yes_x1()) * 0.5f, quit_btn_yc());
    CP_Settings_Fill(hovNo ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("NO", (quit_no_x0() + quit_no_x1()) * 0.5f, quit_btn_yc());
}

void draw_menu_ui(void) {
    float centerX = screenW * 0.5f;
    float centerY = screenH * 0.5f;

    float panelW = screenW * 0.62f;
    float panelH = screenH * 0.70f;
    float panelTop = centerY - panelH * 0.5f;

    draw_cream_panel(centerX, centerY, panelW, panelH);

    if (gameFont) {
        CP_Font_Set(gameFont);
    }

    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    if (showSettings) {
        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
        CP_Settings_TextSize(panelH * 0.108f);
        CP_Font_DrawText("CONTROLS & SETTINGS", centerX, panelTop + panelH * 0.22f);

        CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
        CP_Settings_TextSize(panelH * 0.064f);
        CP_Font_DrawText("A / D  -  Move Left / Right", centerX, panelTop + panelH * 0.43f);
        CP_Font_DrawText("SPACE  -  Jump / Double Jump", centerX, panelTop + panelH * 0.55f);

        CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
        CP_Settings_TextSize(panelH * 0.056f);
        CP_Font_DrawText("Click anywhere on card to go BACK", centerX, panelTop + panelH * 0.73f);
    }
    else {
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

        if (playBtnImage) {
            CP_Image_Draw(playBtnImage, centerX - (btnSize * 0.5f), btnY, btnSize, btnSize, 255);
        }

        if (volBtnImage) {
            float vx = centerX - (btnSize * 0.5f) - btnOffset;
            CP_Image_Draw(volBtnImage, vx, btnY, btnSize, btnSize, muted ? 140 : 255);
            if (muted) {
                float inset = btnSize * 0.125f;
                CP_Settings_Stroke(CP_Color_Create(180, 30, 25, 255));
                CP_Settings_StrokeWeight(panelH * 0.014f);
                CP_Graphics_DrawLine(vx + inset, btnY + inset, vx + btnSize - inset, btnY + btnSize - inset);
                CP_Graphics_DrawLine(vx + btnSize - inset, btnY + inset, vx + inset, btnY + btnSize - inset);
                CP_Settings_NoStroke();
            }
        }

        if (settingsBtnImage) {
            CP_Image_Draw(settingsBtnImage, centerX - (btnSize * 0.5f) + btnOffset, btnY, btnSize, btnSize, 255);
        }
    }

    draw_quit_confirm();
}

void handle_menu_input(void) {
    if (isTransitioning) return;

    if (CP_Input_KeyTriggered(KEY_ESCAPE)) {
        if (showQuitConfirm) showQuitConfirm = false;
        else if (!showSettings) {
            showQuitConfirm = true;
            if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
        }
    }
    if (showQuitConfirm) {
        if (CP_Input_KeyTriggered(KEY_Y) || CP_Input_KeyTriggered(KEY_ENTER)) {
            CP_Engine_Terminate();
            return;
        }
        if (CP_Input_KeyTriggered(KEY_N)) { showQuitConfirm = false; return; }
    }

    if (CP_Input_KeyTriggered(KEY_SPACE) && !showQuitConfirm) {
        isTransitioning = true;
        transitionTimer = 0.0f;
    }

    if (CP_Input_MouseClicked()) {
        if (showQuitConfirm) {
            float mx = CP_Input_GetMouseX();
            float my = CP_Input_GetMouseY();
            if (mx >= quit_yes_x0() && mx <= quit_yes_x1() && my >= quit_btn_y0() && my <= quit_btn_y1()) {
                CP_Engine_Terminate();
            }
            else if (mx >= quit_no_x0() && mx <= quit_no_x1() && my >= quit_btn_y0() && my <= quit_btn_y1()) {
                showQuitConfirm = false;
                play_click();
            }
            return;
        }
        float centerX = screenW * 0.5f;
        float panelW = screenW * 0.62f;
        float panelH = screenH * 0.70f;
        float panelTop = screenH * 0.5f - panelH * 0.5f;

        if (showSettings) {
            if (is_mouse_over(centerX - (panelW * 0.5f), panelTop, panelW, panelH)) {
                showSettings = false;
                play_click();
            }
        }
        else {
            float btnSize = panelH * 0.128f;
            float btnY = panelTop + panelH * 0.58f;
            float btnOffset = panelW * 0.125f;

            float playX = centerX - (btnSize * 0.5f);
            float volX = playX - btnOffset;
            float settingsX = playX + btnOffset;

            if (is_mouse_over(playX, btnY, btnSize, btnSize)) {
                play_click();
                isTransitioning = true;
                transitionTimer = 0.0f;
            }

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
        }
    }
}

static float pause_row_y(int i) { return SY(310.0f + (float)i * 70.0f); }

void draw_pause_menu(void) {
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, screenW, screenH);

    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    draw_cream_panel(screenW * 0.5f, SY(140.0f), SX(420.0f), SY(96.0f));
    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(SF(54.0f));
    CP_Font_DrawText(pausePage == 0 ? "PAUSED" : "OPTIONS", screenW * 0.5f, SY(140.0f));

    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();

    draw_cream_panel(screenW * 0.5f, SY(380.0f), SX(440.0f), SY(300.0f));
    CP_Settings_TextSize(SF(40.0f));
    if (pausePage == 0) {
        const char* rows[] = { "RESUME", "RESTART", "OPTIONS" };
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            bool hover = mx >= SX(450.0f) && mx <= SX(830.0f) && my >= ry - SY(28.0f) && my <= ry + SY(28.0f);
            if (hover) {
                CP_Settings_RectMode(CP_POSITION_CORNER);
                CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
                CP_Graphics_DrawRect(SX(450.0f), ry - SY(28.0f), SX(380.0f), SY(56.0f));
            }
            CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
            CP_Font_DrawText(rows[i], screenW * 0.5f, ry);
        }
    }
    else {
        const char* rows[] = { "MUSIC", "SFX", "BACK" };
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            if (i < 2) {
                float vol = (i == 0) ? musicVol : sfxVol;
                bool hovMinus = mx >= SX(670.0f) && mx <= SX(718.0f) && my >= ry - SY(24.0f) && my <= ry + SY(24.0f);
                bool hovPlus = mx >= SX(806.0f) && mx <= SX(854.0f) && my >= ry - SY(24.0f) && my <= ry + SY(24.0f);
                CP_Settings_RectMode(CP_POSITION_CORNER);
                CP_Settings_Fill(hovMinus ? CP_Color_Create(215, 140, 40, 160) : CP_Color_Create(40, 40, 40, 40));
                CP_Graphics_DrawRect(SX(670.0f), ry - SY(24.0f), SX(48.0f), SY(48.0f));
                CP_Settings_Fill(hovPlus ? CP_Color_Create(215, 140, 40, 160) : CP_Color_Create(40, 40, 40, 40));
                CP_Graphics_DrawRect(SX(806.0f), ry - SY(24.0f), SX(48.0f), SY(48.0f));
                CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
                CP_Font_DrawText("-", SX(694.0f), ry);
                CP_Font_DrawText("+", SX(830.0f), ry);
                char vbuf[16];
                sprintf(vbuf, "%d%%", (int)(vol * 100.0f + 0.5f));
                CP_Font_DrawText(vbuf, SX(762.0f), ry);
                CP_Font_DrawText(rows[i], SX(555.0f), ry);
            }
            else {
                bool hover = mx >= SX(450.0f) && mx <= SX(830.0f) && my >= ry - SY(28.0f) && my <= ry + SY(28.0f);
                if (hover) {
                    CP_Settings_RectMode(CP_POSITION_CORNER);
                    CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
                    CP_Graphics_DrawRect(SX(450.0f), ry - SY(28.0f), SX(380.0f), SY(56.0f));
                }
                CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
                CP_Font_DrawText(rows[i], screenW * 0.5f, ry);
            }
        }
    }

    draw_cream_panel(screenW * 0.5f, SY(600.0f), SX(440.0f), SY(100.0f));
    bool hovMenu = mx >= SX(430.0f) && mx <= SX(630.0f) && my >= SY(572.0f) && my <= SY(628.0f);
    bool hovQuit = mx >= SX(650.0f) && mx <= SX(850.0f) && my >= SY(572.0f) && my <= SY(628.0f);
    CP_Settings_RectMode(CP_POSITION_CORNER);
    if (hovMenu) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(SX(430.0f), SY(572.0f), SX(200.0f), SY(56.0f));
    }
    if (hovQuit) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(SX(650.0f), SY(572.0f), SX(200.0f), SY(56.0f));
    }
    CP_Settings_Fill(hovMenu ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("MENU", SX(530.0f), SY(600.0f));
    CP_Settings_Fill(hovQuit ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("QUIT", SX(750.0f), SY(600.0f));
}

void handle_pause_input(void) {
    float mx = CP_Input_GetMouseX();
    float my = CP_Input_GetMouseY();
    bool clicked = CP_Input_MouseClicked();
    if (pausePage == 0) {
        if (!clicked) return;
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            if (mx >= SX(450.0f) && mx <= SX(830.0f) && my >= ry - SY(28.0f) && my <= ry + SY(28.0f)) {
                if (i == 0) paused = false;
                else if (i == 1) { restart_from_beginning(); paused = false; }
                else { pausePage = 1; if (windowOpenSnd) CP_Sound_Play(windowOpenSnd); }
                play_click();
                return;
            }
        }
        if (mx >= SX(430.0f) && mx <= SX(630.0f) && my >= SY(572.0f) && my <= SY(628.0f)) {
            currentState = STATE_MENU;
            paused = false;
            runStarted = false;
            if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            play_click();
        }
        else if (mx >= SX(650.0f) && mx <= SX(850.0f) && my >= SY(572.0f) && my <= SY(628.0f)) {
            CP_Engine_Terminate();
        }
    }
    else {
        if (!clicked) return;
        for (int i = 0; i < 2; i++) {
            float ry = pause_row_y(i);
            float* vol = (i == 0) ? &musicVol : &sfxVol;
            if (mx >= SX(670.0f) && mx <= SX(718.0f) && my >= ry - SY(24.0f) && my <= ry + SY(24.0f)) {
                *vol -= VOLUME_STEP; if (*vol < 0.0f) *vol = 0.0f;
                apply_volumes();
                play_click();
                return;
            }
            if (mx >= SX(806.0f) && mx <= SX(854.0f) && my >= ry - SY(24.0f) && my <= ry + SY(24.0f)) {
                *vol += VOLUME_STEP; if (*vol > 1.0f) *vol = 1.0f;
                apply_volumes();
                if (i == 1 && stepSnd) CP_Sound_Play(stepSnd);
                else play_click();
                return;
            }
        }
        float ry = pause_row_y(2);
        if (mx >= SX(450.0f) && mx <= SX(830.0f) && my >= ry - SY(28.0f) && my <= ry + SY(28.0f)) { pausePage = 0; play_click(); }
        if (mx >= SX(430.0f) && mx <= SX(630.0f) && my >= SY(572.0f) && my <= SY(628.0f)) {
            currentState = STATE_MENU;
            paused = false;
            runStarted = false;
            if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
            play_click();
        }
        else if (mx >= SX(650.0f) && mx <= SX(850.0f) && my >= SY(572.0f) && my <= SY(628.0f)) {
            CP_Engine_Terminate();
        }
    }
}
