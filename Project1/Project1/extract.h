#pragma once
// Map extraction: Tiled map.json -> tile collision grid (mapData, owned by
// utils.c) + entity pool spawn; map.png sub-rect render.
// (Split out of game.c so game.c keeps gameplay/collision/render-of-actors.
// game.c reaches the pool/table through the externs below.)

#include <stdbool.h>
#include "cprocessing.h"

// -----------------------------------------------------------------------------
// Map object extraction: checkpoints, enemies, traps (map.json objectgroups)
// -----------------------------------------------------------------------------
#define MAX_ENTITIES 72
typedef enum {
    ENT_SAW, ENT_SPIKES, // 0-1
    ENT_CHICKEN, ENT_RINO, // 2-3
    ENT_START, ENT_CHECKPOINT, ENT_END, // 4-6
    ENT_FRUIT_ORANGE, ENT_FRUIT_CHERRIES, ENT_FRUIT_KIWI, ENT_FRUIT_MELON, ENT_FRUIT_STRAWBERRY, // 7-11
    ENT_COUNT // 12
} EntityKind;

typedef struct {
    const char* path;
    const char* altPath;   // flattened / activated variant sheet (checkpoint flag)
    const char* idlePath;  // optional Idle strip; NULL = reuse path
    const char* runPath;   // optional Run (or Fly) strip; NULL = reuse path
    const char* hitPath;   // optional one-shot Hit strip; NULL = instant remove on stomp
    int cellW, cellH;      // frame cell size on the sheet (source px)
    float hbW, hbH;        // hitbox as a fraction of the scaled cell
    bool hazard;           // non-living damager (saw, spikes)
    float speed;           // patrol speed (world px/s); 0 = stationary
    float animFps;         // animation playback speed
} EntityDef;

extern const EntityDef ENTITY_DEFS[ENT_COUNT];

typedef struct {
    EntityKind kind;
    bool active;     // true = still in game. false = object no longer in game (like rino dies, or fruit collected already)
    CP_Image baseImage, altImage;
    CP_Image idleImg, runImg, hitImg; // per-state strips (NULL = reuse baseImage)
    int animState;   // 1 = run, 2 = hit flash
    float stateT;    // time in current animState
    float deathT;    // >0 = stomped, Hit strip playing, counts down to deactivate
    int totalFrames, cellW, cellH;
    float x, y;            // world px, CENTER of the entity
    float sx, sy;          // spawn point (restored on reset)
    float w, h;            // world-px hitbox, centered on (x, y)
    float vx, vy;
    int facing;
    float animT;
    bool grounded;
    bool activated;        // checkpoint reached -> flag out
} Entity;

extern Entity entityPool[MAX_ENTITIES]; // array of 72 slots
extern int entityCount;        // How many used so far (start at 0).
extern float entityScale;   // world px per Tiled px == TILE_SIZE / tileWidth
extern int tileWidth;       // map.json tile px (16); render fallback + load share it
extern int tileHeight;

// Loads and parses map.json exported from Tiled using cJSON
void load_map_json(const char* filepath);

// Renders the tilemap parsed from map.json using Terrain (16x16).png
void render_map_json(void);
