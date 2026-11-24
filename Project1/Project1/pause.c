// memory leak checker
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <stdio.h>
#include "cprocessing.h"
#include "utils.h"  // SCREEN_WIDTH, gameFont, musicVol/sfxVol, apply_volumes, play_click, is_mouse_over
#include "menu.h"   // menu_init/menu_update/menu_exit (pause MENU button returns here)
#include "game.h"   // restart_from_beginning (RESTART button), reload_player_skin, stepSnd (SFX preview)
#include "pause.h"  // own paused/pausePage flags, draw/input entry points
#include "skin.h"   // skin picker page + selected-skin state

// Pause menu 
bool paused = false;
int pausePage = 0;  // 0 = main (Resume/Restart/Options/Skins), 1 = audio options, 2 = skin picker

// Confirm popup for destructive actions (QUIT / RESTART / back-to-MENU):
// 0 none, 1 quit, 2 restart, 3 back-to-menu.
static bool showConfirm = false;
static int confirmAction = 0;

static void draw_confirm_popup(void);  // defined after draw_pause_menu

// Pause menu rows (page 0: main, page 1: audio). Row i center-y = 310 + i * 70 (design space).
static float pause_row_y(int i) { return 310.0f + (float)i * 70.0f; }

void draw_pause_menu(void) {
    // Skin picker owns the whole overlay on page 2 (it draws its own dim +
    // panel, so the pause banner/panels underneath are skipped).
    if (pausePage == 2) {
        skin_picker_draw();
        return;
    }

// Dark overlay over the frozen world
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);

    float centerX = SCREEN_WIDTH * 0.5f;
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    // Banner
    draw_cream_panel(centerX, 140.0f, 420.0f, 96.0f);
    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(54.0f);
    CP_Font_DrawText(pausePage == 0 ? "PAUSED" : "OPTIONS", centerX, 140.0f);

    float rect_x = centerX - 190.0f;
    float rect_w = 380.0f;

    // Top panel (taller on the main page: four rows now)
    draw_cream_panel(centerX, 395.0f, 440.0f, 370.0f);
    CP_Settings_TextSize(40.0f);

    if (pausePage == 0) {
        const char* rows[] = { "RESUME", "RESTART", "OPTIONS", "SKINS" };
        for (int i = 0; i < 4; i++) {
            float ry = pause_row_y(i);
            // "RESUME", "RESTART", "OPTIONS", "SKINS"
            if (is_mouse_over(rect_x, ry - 28.0f, rect_w, 56.0f)) {
                CP_Settings_RectMode(CP_POSITION_CORNER);
                CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
                CP_Graphics_DrawRect(rect_x, ry - 28.0f, rect_w, 56.0f);
            }
            CP_Settings_Fill(is_mouse_over(rect_x, ry - 28.0f, rect_w, 56.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
            CP_Font_DrawText(rows[i], SCREEN_WIDTH * 0.5f, ry);
        }
    }
    else { // OPTIONS area:
        // Audio rows: label + [-] value [+]
        const char* rows[] = { "MUSIC", "SFX", "BACK" };
        for (int i = 0; i < 3; i++) {
            float ry = pause_row_y(i);
            if (i < 2) {
                float vol = (i == 0) ? musicVol : sfxVol;
                
                float minus_x = centerX - 13.0f;
                float plus_x  = centerX + 122.0f;
                float val_x   = centerX + 78.0f;
                float label_x = centerX - 130.0f;

                bool hovMinus = is_mouse_over(minus_x, ry - 24.0f, 48.0f, 48.0f);
                bool hovPlus  = is_mouse_over(plus_x, ry - 24.0f, 48.0f, 48.0f);

                CP_Settings_RectMode(CP_POSITION_CORNER);
                
                // [-] Button
                CP_Settings_Fill(hovMinus ? CP_Color_Create(215, 140, 40, 160) : CP_Color_Create(40, 40, 40, 40));
                CP_Graphics_DrawRect(minus_x, ry - 24.0f, 48.0f, 48.0f);
                
                // [+] Button
                CP_Settings_Fill(hovPlus ? CP_Color_Create(215, 140, 40, 160) : CP_Color_Create(40, 40, 40, 40));
                CP_Graphics_DrawRect(plus_x, ry - 24.0f, 48.0f, 48.0f);

                // Text labels
                CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
                CP_Font_DrawText("-", minus_x + 24.0f, ry);
                CP_Font_DrawText("+", plus_x + 24.0f, ry);
                
                char vbuf[16];
                sprintf(vbuf, "%d%%", (int)(vol * 100.0f + 0.5f));
                CP_Font_DrawText(vbuf, val_x, ry);
                CP_Font_DrawText(rows[i], label_x, ry);
            }
            else {
                // BACK button shares the centered row hitbox.
                bool hover = is_mouse_over(rect_x, ry - 28.0f, rect_w, 56.0f);
                
                if (hover) {
                    CP_Settings_RectMode(CP_POSITION_CORNER);
                    CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
                    CP_Graphics_DrawRect(rect_x, ry - 28.0f, rect_w, 56.0f);
                }
                CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
                CP_Font_DrawText(rows[i], centerX, ry);
            }
        }
    }

    // Bottom panel: MENU | QUIT (moved down to clear the taller top panel)
    draw_cream_panel(centerX, 640.0f, 440.0f, 100.0f);
    CP_Settings_RectMode(CP_POSITION_CORNER);
    float menuButtonX = centerX - 200.0f;
    float quitButtonX = centerX;

    // If mouse hovers over MENU button, draw orange rect
    if (is_mouse_over(menuButtonX, 612.0f, 200.0f, 56.0f)) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        // Format: x-axis, y-axis, width, height
        CP_Graphics_DrawRect(menuButtonX, 612.0f, 200.0f, 56.0f);
    }

    // If mouse hovers over QUIT button, draw orange rect
    if (is_mouse_over(quitButtonX, 612.0f, 200.0f, 56.0f)) {
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(quitButtonX, 612.0f, 200.0f, 56.0f);
    }
    // Menu Text dark gray or orange if mouse hover over it
    CP_Settings_Fill(is_mouse_over(menuButtonX, 612.0f, 200.0f, 56.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("MENU", menuButtonX + 100.0f, 640.0f);
    // Quit button dark gray or orange if mouse hovers over it.
    CP_Settings_Fill(is_mouse_over(quitButtonX, 612.0f, 200.0f, 56.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("QUIT", quitButtonX + 100.0f, 640.0f);

    draw_confirm_popup();  // confirm overlay for QUIT / RESTART / back-to-MENU (no-op unless armed)
}

// Confirm popup (pause menu): dim + cream card + YES / NO, like the main-menu
// quit popup. Title follows the armed action. Drawn every frame from
// draw_pause_menu above — never called from inside a click branch.
static void draw_confirm_popup(void) {
    if (!showConfirm) return;
    float centerX = SCREEN_WIDTH * 0.5f;
    float yesX = centerX - 220.0f;
    float noX = centerX + 60.0f;

    // Dark overlay over the pause menu (same look as the pause dim)
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Card
    draw_cream_panel(centerX, 360.0f, 560.0f, 250.0f);
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    const char* title = "ARE YOU SURE?";
    const char* subtitle = "Are you sure?";
    if (confirmAction == 1) { title = "QUIT GAME?"; subtitle = "Are you sure you want to quit?"; }
    else if (confirmAction == 2) { title = "RESTART RUN?"; subtitle = "Progress will be lost."; }
    else if (confirmAction == 3) { title = "BACK TO MENU?"; subtitle = "Progress will be lost."; }

    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(52.0f);
    CP_Font_DrawText(title, centerX, 300.0f);

    CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
    CP_Settings_TextSize(30.0f);
    CP_Font_DrawText(subtitle, centerX, 348.0f);

    // YES / NO buttons (hover highlight + label, pause-menu style)
    CP_Settings_RectMode(CP_POSITION_CORNER);
    if (is_mouse_over(yesX, 404.0f, 160.0f, 52.0f)) { // "YES" button
        // draw orange near-transparent color rect when user mouse hovers over it
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(yesX, 404.0f, 160.0f, 52.0f);
    }
    if (is_mouse_over(noX, 404.0f, 160.0f, 52.0f)) { // "NO" button
        // draw orange near-transparent color rect when user mouse hovers over it
        CP_Settings_Fill(CP_Color_Create(215, 140, 40, 90));
        CP_Graphics_DrawRect(noX, 404.0f, 160.0f, 52.0f);
    }

    CP_Settings_TextSize(38.0f);
    CP_Settings_Fill(is_mouse_over(yesX, 404.0f, 160.0f, 52.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("YES", yesX + 80.0f, 430.0f);
    CP_Settings_Fill(is_mouse_over(noX, 404.0f, 160.0f, 52.0f) ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
    CP_Font_DrawText("NO", noX + 80.0f, 430.0f);
}

bool pause_confirm_open(void) { return showConfirm; }

void pause_cancel_confirm(void) {
    showConfirm = false;
    confirmAction = 0;
}

// Arms the confirm popup for a destructive action (1 quit, 2 restart, 3 back-to-menu).
// Nothing happens yet — YES in the popup runs it via run_confirmed_action below.
// arm_confirm(1) = QUIT: confirm first, then close the game
// arm_confirm(2) = RESTART: confirm first, then restart from beginning
// arm_confirm(3) = MENU: confirm first, then back to title
static void arm_confirm(int action) {
    confirmAction = action;
    showConfirm = true;
    if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
}

// YES was picked in the popup: run the armed action, then close the popup.
static void run_confirmed_action(void) {
    if (confirmAction == 1) {
        CP_Engine_Terminate();      // QUIT: close the game
    } else if (confirmAction == 2) {
        restart_from_beginning(); paused = false;  // RESTART: from beginning
    } else if (confirmAction == 3) {
        request_menu_transition();  // fade out game before returning to menu
        if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
    }
    showConfirm = false;
    confirmAction = 0;
    play_click();
}

void handle_pause_input(void) {
    // Popup open: only YES / NO (+ Y/ENTER, N) count; pause buttons underneath are blocked.
    // (ESC is caught by game.c's routing, which cancels via pause_cancel_confirm.)
    if (showConfirm) {
        float centerX = SCREEN_WIDTH * 0.5f;
        if (CP_Input_KeyTriggered(KEY_Y) || CP_Input_KeyTriggered(KEY_ENTER)) { run_confirmed_action(); return; }
        if (CP_Input_KeyTriggered(KEY_N)) { pause_cancel_confirm(); return; }
        if (CP_Input_MouseClicked()) {
            if (is_mouse_over(centerX - 220.0f, 404.0f, 160.0f, 52.0f)) run_confirmed_action();  // YES: do it
            else if (is_mouse_over(centerX + 60.0f, 404.0f, 160.0f, 52.0f)) { pause_cancel_confirm(); play_click(); }  // NO: back
        }
        return;
    }
    // Page 2: character picker. Picking a card swaps the player art right away
    // so the new skin is already on screen once the menu is closed.
    if (pausePage == 2) {
        int before = skinIndex;
        bool back = skin_picker_handle_input();
        if (skinIndex != before) reload_player_skin();
        if (back) pausePage = 0;  // BACK click sound is played by the picker
        return;
    }

    bool clicked = CP_Input_MouseClicked();
    float centerX = SCREEN_WIDTH * 0.5f;
    if (pausePage == 0) { // main pause menu: RESUME, RESTART, OPTIONS, SKINS, MENU, QUIT
        if (!clicked) return;
        // RESUME, RESTART, OPTIONS, SKINS click button area:
        for (int i = 0; i < 4; i++) {
            float ry = pause_row_y(i); // 4 buttons are stacked vertically on top of each other: 310, 380, 450, 520
            if (is_mouse_over(centerX - 190.0f, ry - 28.0f, 380.0f, 56.0f)) {
                if (i == 0) paused = false;  // RESUME
                else if (i == 1) { arm_confirm(2); }  // RESTART: confirm first, then from beginning
                else if (i == 2) { pausePage = 1; if (windowOpenSnd) CP_Sound_Play(windowOpenSnd); }  // OPTIONS
                else { pausePage = 2; if (windowOpenSnd) CP_Sound_Play(windowOpenSnd); }  // SKINS
                play_click();
                return;
            }
        }
        // MENU click button area:
        if (is_mouse_over(centerX - 200.0f, 612.0f, 200.0f, 56.0f)) {
            arm_confirm(3);  // confirm first, then back to title
            play_click();
        } // QUIT click button area:
        else if (is_mouse_over(centerX, 612.0f, 200.0f, 56.0f)) {
            arm_confirm(1);  // confirm first, then close the game
            play_click();
        }
    }
    else { // OPTIONS page: audio rows, BACK and the shared MENU / QUIT bar.
        if (!clicked) return;
        for (int i = 0; i < 2; i++) {
            float ry = pause_row_y(i);
            float minusX = centerX - 13.0f;
            float plusX = centerX + 122.0f;
            float* vol = (i == 0) ? &musicVol : &sfxVol;
            if (is_mouse_over(minusX, ry - 24.0f, 48.0f, 48.0f)) {
                *vol -= 0.1f; if (*vol < 0.0f) *vol = 0.0f;
                apply_volumes();
                play_click();
                return;
            }
            if (is_mouse_over(plusX, ry - 24.0f, 48.0f, 48.0f)) {
                *vol += 0.1f; if (*vol > 1.0f) *vol = 1.0f;
                apply_volumes();
                if (i == 1 && stepSnd) CP_Sound_Play(stepSnd);
                else play_click();
                return;
            }
        } 
        float ry = pause_row_y(2);
        if (is_mouse_over(centerX - 190.0f, ry - 28.0f, 380.0f, 56.0f)) { pausePage = 0; play_click(); }  // BACK click button area
        if (is_mouse_over(centerX - 200.0f, 612.0f, 200.0f, 56.0f)) { // MENU click button area
            arm_confirm(3);  // confirm first, then back to title
            play_click();
        } // QUIT click button area:
        else if (is_mouse_over(centerX, 612.0f, 200.0f, 56.0f)) {
            arm_confirm(1);  // confirm first, then close the game
            play_click();
        }
    }
}
