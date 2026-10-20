#pragma once
// Learning quiz (MCQ only): every checkpoint fires one quiz the first time it
// is reached. Answer correctly -> a buff; wrong -> the quiz closes with no
// buff (skip, keep going). One quiz per checkpoint, ever.
// The game loop checks quiz_is_active() and freezes the world (like paused);
// the overlay is drawn last so it sits above everything.

#include <stdbool.h>
#include "utils.h"   // is_mouse_over, play_click, draw_cream_panel, gameFont

bool quiz_is_active(void);          // any overlay open (question panel)
void quiz_reset(void);              // fresh run: close overlay, clear toast
void quiz_start_checkpoint_quiz(void); // checkpoint reached: 1 MCQ -> buff if correct
void quiz_update(float dt);         // toast timer (keeps ticking while world frozen)
void quiz_handle_input(void);       // overlay clicks (game loop calls while active)
void quiz_draw_toast(void);         // small status banner (top-center, ~3s)
void quiz_draw_overlay(void);       // question panel
