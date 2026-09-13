// audio.h
// Sound handle externs, volume state, and audio helper prototypes.
// All audio globals are defined in audio.c.
#pragma once
#include "game_defs.h"

// ---- Volume state ----
extern float musicVol;       // 0.0 – 1.0
extern float sfxVol;         // 0.0 – 1.0
extern bool  muted;          // menu volume-button toggle; overrides both groups
extern int   musicMode;      // 0 = none, 1 = menu, 2 = world

// ---- Music streams ----
extern CP_Sound menuMusic;
extern CP_Sound worldMusic;

// ---- One-shot SFX ----
extern CP_Sound stepSnd;       // footstep / landing thud
extern CP_Sound jumpSnd;       // jump and double-jump
extern CP_Sound respawnSnd;    // every player spawn
extern CP_Sound clickSnd;      // any button press
extern CP_Sound windowOpenSnd; // a menu or panel opens
extern CP_Sound playerHitSnd;  // player stomps an enemy
extern CP_Sound hitSnd;        // player takes a hit
extern CP_Sound deathSnd;
extern CP_Sound coinSnd;       // checkpoint activated / fruit collected

// ---- Functions ----

// Push musicVol / sfxVol / muted to the CProcessing audio groups
void apply_volumes(void);

// Play the UI click sound (null-safe wrapper)
void play_click(void);
