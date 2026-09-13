// audio.c
// Definitions of all audio globals and implementations of volume helpers.
#include "audio.h"

// ---- Volume state ----
float musicVol  = INITIAL_MUSIC_VOL;
float sfxVol    = INITIAL_SFX_VOL;
bool  muted     = false;  // menu volume-button toggle; overrides both groups
int   musicMode = 0;      // 0 none, 1 menu, 2 world

// ---- Music streams ----
CP_Sound menuMusic    = NULL;
CP_Sound worldMusic   = NULL;

// ---- One-shot SFX ----
CP_Sound stepSnd      = NULL;
CP_Sound jumpSnd      = NULL;
CP_Sound respawnSnd   = NULL;
CP_Sound clickSnd     = NULL;
CP_Sound windowOpenSnd = NULL;
CP_Sound playerHitSnd = NULL;
CP_Sound hitSnd       = NULL;
CP_Sound deathSnd     = NULL;
CP_Sound coinSnd      = NULL;

// Push the current musicVol / sfxVol / muted state to the CProcessing audio groups
void apply_volumes(void) {
    CP_Sound_SetGroupVolume(CP_SOUND_GROUP_MUSIC, muted ? 0.0f : musicVol);
    CP_Sound_SetGroupVolume(CP_SOUND_GROUP_SFX,   muted ? 0.0f : sfxVol);
}

// Play the UI click sound (null-safe)
void play_click(void) { if (clickSnd) CP_Sound_Play(clickSnd); }
