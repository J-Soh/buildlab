// game_defs.h
// All game-wide constants (#defines), enumerations, and data structs.
// Every module includes this file. No module-local globals live here.
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cprocessing.h"

// =============================================================================
// Screen / canvas
// The game runs in a 1280x720 window; screenW/H track the live window size.
// Layout is authored for this 1280x720 canvas and scales via SX/SY/SF if the
// window is ever resized.
// =============================================================================
#define DEFAULT_SCREEN_W  1280.0f   // reference canvas width
#define DEFAULT_SCREEN_H   720.0f   // reference canvas height

// Runtime screen size and reference canvas — defined in main.c, extern here so
// every module can call SX/SY/SF without a separate screen.h.
extern float screenW, screenH;
extern float refW,    refH;

// Project a reference-canvas value to the live window (X / Y / font).
static inline float SX(float v) { return v * (screenW / refW); }
static inline float SY(float v) { return v * (screenH / refH); }
static inline float SF(float v) { return v * (screenH / refH); }

// Current state-machine state — defined in main.c.
typedef enum { STATE_MENU, STATE_GAME } GameState;
extern GameState currentState;

// =============================================================================
// Background scrolling
// =============================================================================
// Ambient background scroll: rises continuously, wraps seamlessly (px/sec)
#define BG_SCROLL_SPEED  70.0f

// 1-px overlap added to each bg tile to seal inter-tile cracks that appear
// when the display scales fractional pixels.
#define BG_CRACK_SEAL     1.0f

// =============================================================================
// Tile / world
// =============================================================================
#define TILE_SIZE  48.0f   // Scaled tile size for rendering (source 16px → display 48px)

// Tilemap corner GIDs that look solid in the tileset but are purely visual
// background fill — the player and entities pass through them freely.
// GID 24  = navy ocean fill cell     (row 1, col 23, Terrain (16x16).png)
// GID 112 = dark-navy deep fill cell (row 6, col 15, Terrain (16x16).png)
#define TILE_GID_BG_FILL_1   24
#define TILE_GID_BG_FILL_2  112

// =============================================================================
// Transition
// =============================================================================
#define TRANSITION_DURATION  0.7f   // diamond wipe in + out: total duration in seconds

// =============================================================================
// Player (Virtual Guy) — physics
// =============================================================================
#define PLAYER_SCALE    3.0f    // 32px sprite → 96px displayed, matches 16→48 tiles
#define PLAYER_HB_W    36.0f   // hitbox width  (world px)
#define PLAYER_HB_H    66.0f   // hitbox height (world px)
#define PLAYER_HB_OX   30.0f   // hitbox left-edge offset inside the drawn sprite
#define PLAYER_HB_OY   30.0f   // hitbox top-edge  offset inside the drawn sprite

#define MOVE_SPEED       550.0f
#define JUMP_VEL        -980.0f
#define DOUBLE_JUMP_VEL -860.0f
#define GRAVITY         2800.0f
#define MAX_FALL        1150.0f
#define COYOTE_TIME       0.10f   // seconds of coyote (late-jump) grace after leaving ground
#define JUMP_BUFFER       0.15f   // seconds a pressed SPACE is buffered before landing

// --- Physics solver ---
// Corner inset for wall / ceiling / floor probes: the probe points sit
// PHYS_PROBE_INSET px inward from each hitbox corner so a pixel-wide sliver
// does not register as a wall collision.
#define PHYS_PROBE_INSET  2.0f

// Tiny nudge applied after snapping the player to a tile edge to prevent
// the probe sitting exactly on the boundary next frame.
#define PHYS_SNAP_EPS     0.01f

// Variable-height jump: cap vy to this value on early SPACE release so short
// taps produce a noticeably lower arc than held presses.
#define JUMP_CUT_VY      -350.0f

// Max frame-time clamp to prevent tunnelling through thin tiles on hitches
// (e.g. a debugger pause or system stall).
#define DT_MAX  (1.0f / 30.0f)

// =============================================================================
// Player — animation / feel
// =============================================================================
// Minimum downward velocity at which the landing-thud sound and dust puff fire
#define LANDING_SND_VY         500.0f

// Footstep sound cadence (seconds between sounds while running on the ground)
#define FOOTSTEP_INTERVAL        0.30f

// Minimum |vx| that the code considers the player to be running
// (used for footstep sound gate and run-dust spawning)
#define FOOTSTEP_VX_THRESHOLD   20.0f

// Frame counts and fps for each animation strip
#define PLAYER_IDLE_FPS           8.0f
#define PLAYER_IDLE_FRAMES       11
#define PLAYER_RUN_FPS           12.0f
#define PLAYER_RUN_FRAMES        12
#define PLAYER_DOUBLE_FPS        12.0f
#define PLAYER_DOUBLE_LAST_FRAME  5     // last valid frame index in the double-jump strip

// Invulnerability blink: how many on/off cycles per second the sprite flickers
#define PLAYER_BLINK_RATE        14.0f

// Jump sound playback parameters
#define JUMP_SFX_VOLUME    0.9f
#define JUMP_SFX_PITCH     1.0f    // normal-jump pitch multiplier
#define JUMP2_SFX_PITCH    1.25f   // double-jump pitch (slightly higher = lighter feel)

// =============================================================================
// Dust particles
// =============================================================================
#define MAX_DUST   96

// Upward deceleration applied to every dust particle's vy each frame
#define DUST_GRAVITY        300.0f

// Landing puff — spawned when the player hits the ground after a fall
#define LANDING_DUST_COUNT   6
#define DUST_LAND_SPREAD    14.0f    // horizontal spacing between puff particles (world px)
#define DUST_LAND_LIFE       0.45f
#define DUST_LAND_SIZE      20.0f

// Single-jump puff — spawned at feet on the first jump
#define DUST_JUMP_LIFE      0.35f
#define DUST_JUMP_SIZE      22.0f

// Double-jump burst — scattered ring spawned on the second jump
#define DOUBLE_JUMP_DUST_COUNT   6
#define DUST_JUMP2_SIZE         20.0f

// Run dust — periodic kick while running on the ground
#define DUST_RUN_LIFE       0.4f
#define DUST_RUN_SIZE      18.0f

// Stomp puff — spread from enemy position on a successful stomp
#define STOMP_DUST_COUNT    5
#define STOMP_DUST_VY    -140.0f
#define STOMP_DUST_SIZE    18.0f
#define STOMP_DUST_LIFE     0.45f

// =============================================================================
// Shadow — cast downward from the player's feet
// =============================================================================
#define SHADOW_MAX_DIST      300.0f   // feet-to-ground distance at which shadow disappears
#define SHADOW_PROBE_START     4.0f   // start of the downward scan (world px below feet)
#define SHADOW_PROBE_STEP      6.0f   // y increment per scan step
#define SHADOW_MIN_SIZE       20.0f   // drawn size (px) when far from the ground
#define SHADOW_SIZE_RANGE     28.0f   // additional size added when right on the ground
#define SHADOW_MIN_ALPHA      60      // alpha when far from ground
#define SHADOW_ALPHA_RANGE   140      // additional alpha when close to ground
#define SHADOW_ABOVE_GROUND   10.0f   // shadow drawn this many px above the ground top
#define SHADOW_DRAW_HEIGHT    14.0f   // drawn height of the shadow sprite

// =============================================================================
// Combat — player getting hurt
// =============================================================================
// Invulnerability seconds granted after taking a hit (blinking sprite)
#define HIT_INVULN_TIME       1.4f

// Seconds player input is locked while knockback velocity coasts
#define HIT_LOCK_TIME         0.5f

// Knockback force magnitude; direction is derived at the call site
#define HIT_KNOCKBACK_VX    300.0f

// Only override the player's vy if it is above this threshold (avoid cancelling
// an already-strong upward jump)
#define HIT_KB_VY_THRESHOLD -200.0f

// Upward velocity kick applied on any hit
#define HIT_KNOCKBACK_VY   -300.0f

// Invulnerability granted right after a full death + respawn
#define DEATH_INVULN_TIME     1.2f

// =============================================================================
// Combat — stomping enemies
// =============================================================================
// Player must be moving at least this fast downward (vy) for the contact to
// register as a stomp rather than a side-hit
#define STOMP_MIN_VY         80.0f

// Vertical overlap forgiveness: the player's bottom can be up to STOMP_LENIENCY
// px into the enemy's top and still count as a stomp (pixel-perfect feel)
#define STOMP_LENIENCY       16.0f

// Player's new vy immediately after a successful stomp (bounce)
#define STOMP_BOUNCE_VY    -470.0f

// =============================================================================
// Enemy AI — walker (Chicken / Rino)
// =============================================================================
// Half-width/height of the rectangular detection zone in front of the enemy.
// Player must be inside AND on the facing side to trigger a charge.
#define WALKER_AGGRO_X       480.0f
#define WALKER_AGGRO_Y       110.0f

// Speed multiplier applied when the enemy is actively charging the player
#define WALKER_CHARGE_MULT     2.5f

// Seconds the Hit strip is shown after the enemy hurts the player
// (stomp-deaths use deathT instead)
#define ENT_HIT_FLASH_DUR      0.5f

// Entity gravity + terminal velocity (entities fall slightly slower than the
// player to prevent them tunnelling through thin platforms on steep slopes)
#define ENT_GRAVITY         2400.0f
#define ENT_MAX_FALL         950.0f

// Grounding scan — used at spawn to seat enemies on the correct tile
#define ENT_GROUND_STEP        4.0f          // y step per iteration
#define ENT_GROUND_SCAN  (TILE_SIZE * 3.0f)  // max downward scan distance

// Ledge detection — used to reverse patrol direction at platform edges
#define ENT_LEDGE_PROBE        6.0f   // vertical look-ahead past hitbox bottom
#define ENT_LEDGE_AHEAD        4.0f   // additional forward px past hitbox edge

// =============================================================================
// Enemy AI — Bat sleeper
// =============================================================================
// Wake-up trigger radius (≈ 8 tiles); raise for earlier ambushes.
#define BAT_AGGRO_RADIUS     344.0f

// Extended aggro radius while the bat is actively chasing (so it doesn't
// immediately disengage the moment the player backs up slightly)
#define BAT_AGGRO_CHASE_MULT   1.5f

// Chase / return speed toward player or perch
#define BAT_CHASE_SPEED      150.0f

// Ceiling In alert animation length before the bat starts chasing
#define BAT_EMERGE_TIME        0.45f

// If the bat is within this many px of its perch while returning, snap it home
#define BAT_HOME_SNAP_DIST    10.0f

// Ceiling scan at spawn: step size and maximum distance upward to search
#define BAT_PROBE_STEP         4.0f
#define BAT_CEILING_SCAN  (TILE_SIZE * 10.0f)

// Fraction of the bat's hitbox height that is tucked under the ceiling while perched
#define BAT_CEILING_TUCK       0.45f

// =============================================================================
// Enemy AI — Plant shooter
// =============================================================================
// Seconds into the attack animation when the seed bullet is actually fired
#define PLANT_FIRE_TIME        0.35f

// Time at which the attack animation cycle is considered complete
#define PLANT_ATK_END          0.67f

// Idle cooldown between attack cycles
#define PLANT_COOLDOWN         1.6f

// Rectangular aggro range: player must be within these half-extents for the
// plant to start an attack
#define PLANT_AGGRO_X        560.0f
#define PLANT_AGGRO_Y        220.0f

// =============================================================================
// Bullet projectile (Plant seeds)
// =============================================================================
#define MAX_BULLETS    12
#define BULLET_SPEED  420.0f

// Seconds before a bullet auto-despawns (prevents stale bullets living forever
// if they miss all walls)
#define BULLET_LIFE     3.0f

// Extra px added to each side of the player AABB for bullet contact detection
// (makes bullets feel fair without being too punishing)
#define BULLET_HIT_PADDING  14.0f

// Impact effect pool and animation timing
#define MAX_BULLET_FX       8
#define BULLET_FX_TIMEOUT   0.3f    // total duration of the impact pieces anim
#define BULLET_FX_MID       0.15f   // time threshold to advance to the 2nd impact frame

// =============================================================================
// Fruit pickup sparkle (Collected.png — 6-frame one-shot)
// =============================================================================
#define MAX_PICKUP_FX   16
#define PICKUP_FRAMES    6
#define PICKUP_FPS      18.0f

// =============================================================================
// Rendering / culling margins
// =============================================================================
// Entity / bullet / pickup draw calls are skipped when the screen-space
// position is more than this many px outside the viewport.
#define ENT_CULL_MARGIN     192.0f
#define BULLET_CULL_MARGIN   60.0f
#define PICKUP_CULL_MARGIN   64.0f

// =============================================================================
// Audio / volume
// =============================================================================
#define INITIAL_MUSIC_VOL   1.0f   // default music volume (0.0 – 1.0)
#define INITIAL_SFX_VOL     0.7f   // default SFX volume
#define VOLUME_STEP         0.1f   // increment per pause-menu +/- click

// =============================================================================
// World / camera out-of-bounds respawn buffers
// =============================================================================
// If the player is more than WORLD_FALL_EXTRA px below the bottom of the map
// (or WORLD_SIDE_EXTRA px past either side) they are respawned immediately.
#define WORLD_FALL_EXTRA   200.0f
#define WORLD_SIDE_EXTRA   600.0f

// =============================================================================
// HUD
// =============================================================================
#define MAX_HEARTS    5   // maximum number of heart slots in the HUD

// =============================================================================
// Entity pool
// =============================================================================
#define MAX_ENTITIES  72  // maximum simultaneous map objects (enemies + traps + items)

// =============================================================================
// Structs
// =============================================================================

// Player character state
typedef struct {
    float x, y;        // hitbox top-left, world px
    float vx, vy;
    bool grounded;
    int jumps;         // 0 = ground, 1 = single used, 2 = double used
    int facing;        // 1 = right, -1 = left
    float coyote, buffer;
    float animT;       // frame clock
    float stepT;       // footstep clock
    float lockT, kbx;  // hurt knockback: input locked, forced vx decays
    float invulnT;     // post-damage invulnerability timer
    bool doubleAnim;   // latched while double-jump anim plays
    float doubleAnimT;
} Player;

// Single dust particle
typedef struct {
    bool active;
    float x, y, vx, vy, life, maxLife, size;
} Dust;

// Plant seed bullet
typedef struct { bool active; float x, y, vx, vy, life; } Bullet;

// Bullet impact effect (two-frame Bullet Pieces animation)
typedef struct { bool active; float x, y, t; } BulletFX;

// Fruit pickup sparkle effect (Collected.png one-shot)
typedef struct { bool active; float x, y, t; } PickupFX;

// All map-placed entity types — must match Tiled object-layer names and the
// ENTITY_DEFS lookup table in entity.c
typedef enum {
    ENT_SAW, ENT_SPIKES,                                       // 0-1  hazards
    ENT_PLANT, ENT_CHICKEN, ENT_BAT, ENT_RINO,                 // 2-5  enemies
    ENT_START, ENT_CHECKPOINT, ENT_END,                        // 6-8  checkpoints
    ENT_FRUIT_ORANGE, ENT_FRUIT_CHERRIES, ENT_FRUIT_KIWI,     // 9-11
    ENT_FRUIT_MELON, ENT_FRUIT_STRAWBERRY,                     // 12-13 fruits
    ENT_COUNT                                                   // 14   sentinel
} EntityKind;

// Static definition table: one row per EntityKind, describes art + physics.
// The full array is defined in entity.c.
typedef struct {
    const char* path;
    const char* altPath;    // flattened / activated variant sheet (checkpoint flag)
    const char* idlePath;   // optional Idle strip; NULL = reuse path (Chicken/Bat/Rino)
    const char* runPath;    // optional Run (or Fly) strip; NULL = reuse path
    const char* hitPath;    // optional one-shot Hit strip; NULL = instant remove on stomp
    int cellW, cellH;       // frame cell size on the sheet (source px)
    float hbW, hbH;         // hitbox as a fraction of the scaled cell
    bool hazard;            // non-living damager (saw, spikes)
    bool flies;             // bat: no gravity, hovers
    float speed;            // patrol speed (world px/s); 0 = stationary
    float animFps;          // animation playback speed
} EntityDef;

// Runtime entity instance — one slot per map-placed object
typedef struct {
    EntityKind kind;
    bool active;
    CP_Image baseImage, altImage;
    CP_Image idleImg, runImg, hitImg;   // per-state strips (NULL = reuse baseImage)
    CP_Image emergeImg;                 // bat only: Ceiling In alert strip on detection
    int animState;    // 0 = idle/perched, 1 = run/fly, 2 = hit flash, 3 = bat emerging
    float stateT;     // time in current animState
    float deathT;     // > 0 = stomped, Hit strip playing, counts down to deactivate
    int totalFrames, cellW, cellH;
    float x, y;             // world px, CENTER of the entity
    float sx, sy;           // spawn point (restored on reset)
    float w, h;             // world-px hitbox, centred on (x, y)
    float vx, vy;
    int facing;
    float animT;
    float atkT, coolT;      // plant shooter: attack clock + cooldown
    bool attacking, fired;  // attack anim running / bullet released this cycle
    bool grounded;
    bool activated;         // checkpoint reached → flag out
} Entity;
