#pragma once
// Learning quiz (MCQ only): checkpoints teach multiplication topics, the End
// gate asks one question per topic actually touched (wrong = retry, right =
// +25 pts), then a 1-of-3 buff choice with tradeoffs. A fatal hit (hearts <= 0)
// instead opens one last-chance question: right = survive with 1 heart,
// wrong = death + checkpoint respawn.
// The game loop checks quiz_is_active() and freezes the world (like paused);
// the overlay is drawn last so it sits above everything.

#include <stdbool.h>

bool quiz_is_active(void);          // any overlay open (end quiz / buffs / death quiz)
void quiz_reset(void);              // fresh run: forget topics, close overlays, clear toast
void quiz_on_checkpoint(void);      // checkpoint activated: teach next topic + fact toast
void quiz_start_end_quiz(void);     // End gate touched: quiz learned topics, then buff choice
void quiz_start_death_quiz(void);   // fatal hit: last-chance question (wrong = die)
void quiz_update(float dt);         // toast timer (keeps ticking while world frozen)
void quiz_handle_input(void);       // overlay clicks (game loop calls while active)
void quiz_draw_toast(void);         // "LEARNED: 7 x 8 = 56" banner (top-center)
void quiz_draw_overlay(void);       // question panel or buff-choice panel
