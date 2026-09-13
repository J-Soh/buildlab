// entity.h
// Entity pool globals, entity-system function prototypes, and projectile/pickup exports.
// All entity globals are defined in entity.c.
#pragma once
#include "game_defs.h"

// ---- Entity pool ----
extern Entity entityPool[MAX_ENTITIES];  // array of MAX_ENTITIES slots
extern int    entityCount;               // how many slots are in use

// ---- Static definition table ----
// One row per EntityKind — art paths, cell dimensions, hitbox fractions, AI tuning.
extern const EntityDef ENTITY_DEFS[ENT_COUNT];

// ---- Plant projectile art ----
extern CP_Image plantAtkImg;   // Attack (44x42).png, 8 frames
extern CP_Image bulletImg;     // Bullet 16x16
extern CP_Image piecesImg;     // Bullet Pieces 32x16, 2 frames (impact)

// ---- Bullet pool ----
extern Bullet   bullets[MAX_BULLETS];
extern BulletFX bulletFx[MAX_BULLET_FX];

// ---- Pickup sparkle pool ----
// Collected.png (32x32 cells, 6-frame one-shot). Spawned where a fruit is
// collected, plays once, then frees its slot.
extern PickupFX  pickupFx[MAX_PICKUP_FX];
extern CP_Image  collectedImg;

// ---- Functions ----

// Map the Tiled object-layer name to an EntityKind
EntityKind kind_from_layer_name(const char* name);

// Instantiate one map-placed object at world coords (map px × entityScale).
// Returns the pool slot, or NULL when full or kind is invalid.
Entity* spawn_entity(EntityKind kind, float wx, float wy);

// Move every living object (gravity + patrol + bat AI + plant shooter)
void update_entities(float dt);

// Draw all active entities at their world positions, camera-relative
void render_entities(void);

// Player vs map objects: checkpoints, enemy stomps, damage
void check_player_entity_collisions(void);

// Deal one heart of damage to the player (iframe-gated); handles death + respawn
void hurt_player(void);

// ---- Bullet projectile system ----
// Seeds fly straight, pop into pieces on tiles, hurt on touch (iframe-gated)
void update_bullets(float dt);
void draw_bullets(void);

// ---- Pickup sparkle system ----
// Spawn a one-shot Collected sparkle at world position (wx, wy)
void spawn_pickup_fx(float wx, float wy);

// ---- JSON object-layer parsing (called by load_map_json in map.c) ----
// These functions walk the cJSON tree produced by the Tiled JSON export and
// populate the entity pool with all placed objects.
// (Forward declaration uses void* to avoid pulling cJSON.h into this header;
//  entity.c casts the pointer back to cJSON* internally.)
void process_layers_world_entities(void* layersJson);
