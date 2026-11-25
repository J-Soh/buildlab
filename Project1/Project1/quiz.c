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

// memory leak checker
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <stdio.h>
#include <string.h>
#include "cprocessing.h"
#include "utils.h"    // hearts/maxHearts, score/buffs/muls, UI helpers
#include "quiz.h"     // own overlay state + entry points

typedef struct {
    char question[48];
    char choices[4][12];
    int answerIdx;
} QuizQuestion;

static const QuizQuestion QUESTION_BANK[] = {
    { "2 x 9 = ?",  { "18", "16", "20", "12" }, 0 },
    { "3 x 7 = ?",  { "14", "24", "21", "18" }, 2 },
    { "2 x 6 = ?",  { "10", "14", "8", "12" },  3 },
    { "3 x 8 = ?",  { "24", "21", "16", "18" }, 0 },
    { "4 x 6 = ?",  { "20", "24", "28", "16" }, 1 },
    { "5 x 7 = ?",  { "30", "40", "32", "35" }, 3 },
    { "4 x 8 = ?",  { "32", "28", "36", "24" }, 0 },
    { "5 x 9 = ?",  { "36", "50", "40", "45" }, 3 },
    { "7 x 8 = ?",  { "48", "54", "56", "64" }, 2 },
    { "8 x 6 = ?",  { "54", "42", "48", "56" }, 3 },
    { "7 x 9 = ?",  { "56", "63", "72", "54" }, 1 },
    { "8 x 8 = ?",  { "64", "60", "72", "56" }, 0 },
    { "9 x 9 = ?",  { "72", "81", "90", "88" }, 1 },
    { "11 x 6 = ?", { "60", "72", "66", "56" }, 2 },
    { "9 x 7 = ?",  { "63", "54", "72", "56" }, 0 },
    { "11 x 8 = ?", { "96", "84", "88", "80" }, 2 },
};
#define BANK_SIZE ((int)(sizeof(QUESTION_BANK) / sizeof(QUESTION_BANK[0])))

static bool quizOpen = false;         // question panel showing (world frozen)
static bool needBuffPick = false;     // answered right: pick 1 of 3 buffs next
static const QuizQuestion* currentQ = NULL;

static float toastT = 0.0f;
static char toastText[64] = "";

static void set_toast(const char* text) {
    snprintf(toastText, sizeof(toastText), "%s", text);
    toastT = 3.0f;
}

bool quiz_is_active(void) {
    return quizOpen;
}

void quiz_reset(void) {
    quizOpen = false;
    needBuffPick = false;
    currentQ = NULL;
    toastT = 0.0f;
    toastText[0] = '\0';
}

void quiz_start_checkpoint_quiz(void) {
    if (quiz_is_active()) return;
    currentQ = &QUESTION_BANK[(int)CP_Random_RangeFloat(0.0f, (float)BANK_SIZE - 0.001f)];
    quizOpen = true;
    if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
}

static void apply_buff(int i) {
    if (i == 0) {
        buffs.fortitude = true;
        maxHearts += 2;
        hearts = maxHearts;
        enemySpeedMul = 1.2f;
        set_toast("BUFF: FORTITUDE");
    } else {
        buffs.swift = true;
        moveSpeedMul = 1.2f;
        set_toast("BUFF: SWIFT FEET");
    }
    needBuffPick = false;
    quizOpen = false;
    currentQ = NULL;
}

void quiz_update(float dt) {
    if (toastT > 0.0f) {
        toastT -= dt;
        if (toastT < 0.0f) toastT = 0.0f;
    }
}

void quiz_handle_input(void) {
    if (!CP_Input_MouseClicked()) return;

    if (needBuffPick) {
        for (int i = 0; i < 2; i++) {
            float ry = 300.0f + (float)i * 100.0f;
            if (is_mouse_over(460.0f, ry, 680.0f, 84.0f)) {
                apply_buff(i);
                play_click();
                return;
            }
        }
        return;
    }

    if (!currentQ) return;
    for (int i = 0; i < 4; i++) {
        float ry = 360.0f + (float)i * 62.0f;
        if (!is_mouse_over(510.0f, ry, 580.0f, 52.0f)) continue;
        play_click();
        if (i == currentQ->answerIdx) {
            needBuffPick = true;   // correct -> choose a reward
            currentQ = NULL;
        } else {
            quizOpen = false;      // wrong -> close, no buff (skip)
            currentQ = NULL;
            set_toast("NO BUFF THIS TIME");
        }
        return;
    }
}

void quiz_draw_toast(void) {
    if (toastT <= 0.0f || toastText[0] == '\0') return;
    draw_cream_panel(SCREEN_WIDTH * 0.5f, 92.0f, 540.0f, 64.0f);
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);
    CP_Settings_TextSize(28.0f);
    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Font_DrawText(toastText, SCREEN_WIDTH * 0.5f, 92.0f);
}

static void draw_question_panel(void) {
    draw_cream_panel(SCREEN_WIDTH * 0.5f, 450.0f, 780.0f, 560.0f);
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(44.0f);
    CP_Font_DrawText("CHECKPOINT QUIZ", SCREEN_WIDTH * 0.5f, 230.0f);

    CP_Settings_Fill(CP_Color_Create(40, 40, 40, 255));
    CP_Settings_TextSize(36.0f);
    CP_Font_DrawText(currentQ->question, SCREEN_WIDTH * 0.5f, 310.0f);

    for (int i = 0; i < 4; i++) {
        float ry = 360.0f + (float)i * 62.0f;
        bool hover = is_mouse_over(510.0f, ry, 580.0f, 52.0f);
        CP_Settings_RectMode(CP_POSITION_CORNER);
        CP_Settings_Fill(hover ? CP_Color_Create(215, 140, 40, 90) : CP_Color_Create(40, 40, 40, 25));
        CP_Graphics_DrawRect(510.0f, ry, 580.0f, 52.0f);
        CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
        CP_Settings_TextSize(28.0f);
        CP_Font_DrawText(currentQ->choices[i], SCREEN_WIDTH * 0.5f, ry + 26.0f);
    }
}

static void draw_buff_panel(void) {
    static const char* names[2] = { "FORTITUDE", "SWIFT FEET" };
    static const char* descs[2] = {
        "+2 Max Hearts  |  Enemies 20% Faster",
        "+20% Move Speed",
    };
    draw_cream_panel(SCREEN_WIDTH * 0.5f, 450.0f, 760.0f, 560.0f);
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(44.0f);
    CP_Font_DrawText("CORRECT! CHOOSE A BUFF", SCREEN_WIDTH * 0.5f, 220.0f);

    for (int i = 0; i < 2; i++) {
        float ry = 300.0f + (float)i * 100.0f;
        bool hover = is_mouse_over(460.0f, ry, 680.0f, 84.0f);
        CP_Settings_RectMode(CP_POSITION_CORNER);
        CP_Settings_Fill(hover ? CP_Color_Create(215, 140, 40, 90) : CP_Color_Create(40, 40, 40, 25));
        CP_Graphics_DrawRect(460.0f, ry, 680.0f, 84.0f);
        CP_Settings_Fill(hover ? CP_Color_Create(180, 50, 40, 255) : CP_Color_Create(40, 40, 40, 255));
        CP_Settings_TextSize(30.0f);
        CP_Font_DrawText(names[i], SCREEN_WIDTH * 0.5f, ry + 26.0f);
        CP_Settings_TextSize(18.0f);
        CP_Font_DrawText(descs[i], SCREEN_WIDTH * 0.5f, ry + 60.0f);
    }
}

void quiz_draw_overlay(void) {
    if (!quizOpen && !needBuffPick) return;
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (needBuffPick) draw_buff_panel();
    else draw_question_panel();
}
