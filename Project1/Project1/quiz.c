// memory leak checker
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <stdio.h>
#include <string.h>
#include "cprocessing.h"
#include "utils.h"    // hearts/maxHearts, levelComplete, score/buffs/muls, UI helpers
#include "game.h"     // reset_run (wrong death answer), respawnSnd (survival)
#include "player.h"   // player state (death-quiz survival hop)
#include "quiz.h"     // own overlay state + entry points

typedef struct {
    int topicId;
    char question[48];
    char choices[4][12];
    int answerIdx;
} QuizQuestion;

#define NUM_TOPICS 4
#define MAX_LEARNED 8
static const char* TOPIC_FACTS[NUM_TOPICS] = {
    "2 x 9 = 18", "4 x 6 = 24", "7 x 8 = 56", "9 x 9 = 81"
};

static const QuizQuestion QUESTION_BANK[] = {
    { 0, "2 x 9 = ?",  { "18", "16", "20", "12" }, 0 },
    { 0, "3 x 7 = ?",  { "14", "24", "21", "18" }, 2 },
    { 0, "2 x 6 = ?",  { "10", "14", "8", "12" },  3 },
    { 0, "3 x 8 = ?",  { "24", "21", "16", "18" }, 0 },
    { 1, "4 x 6 = ?",  { "20", "24", "28", "16" }, 1 },
    { 1, "5 x 7 = ?",  { "30", "40", "32", "35" }, 3 },
    { 1, "4 x 8 = ?",  { "32", "28", "36", "24" }, 0 },
    { 1, "5 x 9 = ?",  { "36", "50", "40", "45" }, 3 },
    { 2, "7 x 8 = ?",  { "48", "54", "56", "64" }, 2 },
    { 2, "8 x 6 = ?",  { "54", "42", "48", "56" }, 3 },
    { 2, "7 x 9 = ?",  { "56", "63", "72", "54" }, 1 },
    { 2, "8 x 8 = ?",  { "64", "60", "72", "56" }, 0 },
    { 3, "9 x 9 = ?",  { "72", "81", "90", "88" }, 1 },
    { 3, "11 x 6 = ?", { "60", "72", "66", "56" }, 2 },
    { 3, "9 x 7 = ?",  { "63", "54", "72", "56" }, 0 },
    { 3, "11 x 8 = ?", { "96", "84", "88", "80" }, 2 },
};
#define BANK_SIZE ((int)(sizeof(QUESTION_BANK) / sizeof(QUESTION_BANK[0])))

typedef enum { QUIZ_NONE, QUIZ_END_Q, QUIZ_END_BUFFS, QUIZ_DEATH } QuizMode;

static QuizMode quizMode = QUIZ_NONE;
static int learnedTopics[MAX_LEARNED];
static int learnedCount = 0;
static int checkpointCount = 0;
static const QuizQuestion* quizPool[3];
static int quizTopicCount = 0;
static int quizQIdx = 0;
static const QuizQuestion* currentQ = NULL;
static bool quizRetry = false;

static float toastT = 0.0f;
static char toastText[64] = "";

static void set_toast(const char* text) {
    snprintf(toastText, sizeof(toastText), "%s", text);
    toastT = 3.0f;
}

bool quiz_is_active(void) {
    return quizMode != QUIZ_NONE;
}

void quiz_reset(void) {
    quizMode = QUIZ_NONE;
    learnedCount = 0;
    checkpointCount = 0;
    quizTopicCount = 0;
    quizQIdx = 0;
    currentQ = NULL;
    quizRetry = false;
    toastT = 0.0f;
    toastText[0] = '\0';
}

static const QuizQuestion* random_question_for_topic(int topicId) {
    int matches[8];
    int n = 0;
    for (int i = 0; i < BANK_SIZE && n < 8; i++) {
        if (QUESTION_BANK[i].topicId == topicId) matches[n++] = i;
    }
    if (n == 0) return &QUESTION_BANK[0];
    int pick = (int)CP_Random_RangeFloat(0.0f, (float)n - 0.001f);
    return &QUESTION_BANK[matches[pick]];
}

void quiz_on_checkpoint(void) {
    int topic = checkpointCount % NUM_TOPICS;
    checkpointCount++;
    bool known = false;
    for (int i = 0; i < learnedCount; i++) {
        if (learnedTopics[i] == topic) known = true;
    }
    if (!known && learnedCount < MAX_LEARNED) learnedTopics[learnedCount++] = topic;
    char buf[64];
    snprintf(buf, sizeof(buf), "LEARNED: %s", TOPIC_FACTS[topic]);
    set_toast(buf);
}

void quiz_start_end_quiz(void) {
    if (quiz_is_active() || levelComplete) return;
    quizTopicCount = 0;
    for (int i = 0; i < learnedCount && quizTopicCount < 3; i++) {
        int t = learnedTopics[i];
        bool dup = false;
        for (int j = 0; j < quizTopicCount; j++) {
            if (quizPool[j]->topicId == t) dup = true;
        }
        if (!dup) quizPool[quizTopicCount++] = random_question_for_topic(t);
    }
    quizQIdx = 0;
    quizRetry = false;
    if (quizTopicCount == 0) {
        quizMode = QUIZ_END_BUFFS;
        currentQ = NULL;
    } else {
        currentQ = quizPool[0];
        quizMode = QUIZ_END_Q;
        if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
    }
}

void quiz_start_death_quiz(void) {
    if (quiz_is_active()) return;
    currentQ = &QUESTION_BANK[(int)CP_Random_RangeFloat(0.0f, (float)BANK_SIZE - 0.001f)];
    quizRetry = false;
    quizMode = QUIZ_DEATH;
    if (windowOpenSnd) CP_Sound_Play(windowOpenSnd);
}

static void apply_buff(int i) {
    if (i == 0) {
        buffs.fortitude = true;
        maxHearts += 2;
        hearts = maxHearts;
        enemySpeedMul = 1.2f;
        set_toast("BUFF: FORTITUDE");
    } else if (i == 1) {
        buffs.arsenal = true;
        bulletCost = 10;
        if (maxHearts > 1) maxHearts -= 1;
        if (hearts > maxHearts) hearts = maxHearts;
        set_toast("BUFF: ARSENAL");
    } else {
        buffs.swift = true;
        moveSpeedMul = 1.2f;
        set_toast("BUFF: SWIFT FEET");
    }
    quizMode = QUIZ_NONE;
    currentQ = NULL;
    quizRetry = false;
    levelComplete = true;
}

static void death_quiz_survive(void) {
    quizMode = QUIZ_NONE;
    currentQ = NULL;
    quizRetry = false;
    hearts = 1;
    player.invulnT = 1.2f;
    player.vy = -300.0f;
    if (respawnSnd) CP_Sound_Play(respawnSnd);
    set_toast("PASSED! YOU SURVIVE");
}

static void death_quiz_die(void) {
    quizMode = QUIZ_NONE;
    currentQ = NULL;
    quizRetry = false;
    if (deathSnd) CP_Sound_Play(deathSnd);
    reset_run();
    player.invulnT = 1.2f;
}

void quiz_update(float dt) {
    if (toastT > 0.0f) {
        toastT -= dt;
        if (toastT < 0.0f) toastT = 0.0f;
    }
}

void quiz_handle_input(void) {
    if (!CP_Input_MouseClicked()) return;

    if (quizMode == QUIZ_END_BUFFS) {
        for (int i = 0; i < 3; i++) {
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
        if (i == currentQ->answerIdx) {
            play_click();
            if (quizMode == QUIZ_DEATH) {
                death_quiz_survive();
            } else {
                score += 25;
                set_toast("CORRECT! +25 PTS");
                quizQIdx++;
                quizRetry = false;
                if (quizQIdx >= quizTopicCount) {
                    quizMode = QUIZ_END_BUFFS;
                    currentQ = NULL;
                } else {
                    currentQ = quizPool[quizQIdx];
                }
            }
        } else if (quizMode == QUIZ_DEATH) {
            death_quiz_die();
        } else {
            quizRetry = true;
            play_click();
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
    CP_Font_DrawText(quizMode == QUIZ_DEATH ? "LAST CHANCE!" : "LEVEL QUIZ",
                     SCREEN_WIDTH * 0.5f, 230.0f);

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

    if (quizRetry) {
        CP_Settings_Fill(CP_Color_Create(180, 30, 25, 255));
        CP_Settings_TextSize(26.0f);
        CP_Font_DrawText("Not quite - try again!", SCREEN_WIDTH * 0.5f, 648.0f);
    }
}

static void draw_buff_panel(void) {
    static const char* names[3] = { "FORTITUDE", "ARSENAL", "SWIFT FEET" };
    static const char* descs[3] = {
        "+2 Max Hearts  |  Enemies 20% Faster",
        "Cheaper Bullets (10 pts)  |  -1 Max Heart",
        "+20% Move Speed  |  No Double Jump",
    };
    draw_cream_panel(SCREEN_WIDTH * 0.5f, 450.0f, 760.0f, 560.0f);
    if (gameFont) CP_Font_Set(gameFont);
    CP_Settings_TextAlignment(CP_TEXT_ALIGN_H_CENTER, CP_TEXT_ALIGN_V_MIDDLE);

    CP_Settings_Fill(CP_Color_Create(180, 50, 40, 255));
    CP_Settings_TextSize(44.0f);
    CP_Font_DrawText("CHOOSE A BUFF", SCREEN_WIDTH * 0.5f, 220.0f);

    for (int i = 0; i < 3; i++) {
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
    if (quizMode == QUIZ_NONE) return;
    CP_Settings_RectMode(CP_POSITION_CORNER);
    CP_Settings_Fill(CP_Color_Create(0, 0, 0, 140));
    CP_Graphics_DrawRect(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (quizMode == QUIZ_END_BUFFS) draw_buff_panel();
    else draw_question_panel();
}
