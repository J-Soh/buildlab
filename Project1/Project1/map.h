// map.h
// Tile-map globals, collision queries, JSON load, and camera-relative rendering.
// All map globals are defined in map.c.
#pragma once
#include "game_defs.h"

// ---- Map tile data ----
extern int   mapWidth, mapHeight;
extern int   tileWidth, tileHeight;
extern int*  mapData;          // flat GID array: mapData[ty * mapWidth + tx]

// ---- Tilesheet (fallback renderer when map.png is missing) ----
// Terrain (16x16).png: 384 x 192 px by default
extern CP_Image terrainSheet;
extern float    sheetW, sheetH;

// ---- Pre-rendered map image ----
// Tiled → Export as Image at 1× = 3200×1600 px.
// Drawn as one camera sub-rect per frame; map.json tile data is still loaded
// and used for collision only.
extern CP_Image mapImage;

// ---- Camera (world-pixel position of the viewport's top-left corner) ----
extern float camX, camY;

// ---- Scale factor ----
// world px per Tiled px  ==  TILE_SIZE / tileWidth  ==  3.0
extern float entityScale;

// ---- Functions ----

// True if a GID is a background-fill cell that looks solid but passes physics
bool tile_is_bg_fill(int gid);

// True if tile cell (tx, ty) is solid. Bounds-aware: walls + floor outside
// the map are solid; open sky above is not.
bool is_solid_cell(int tx, int ty);

// True if world point (wx, wy) falls inside a solid tile
bool is_solid_px(float wx, float wy);

// Scan the map for the leftmost lowest open pocket with headroom + a solid
// floor beneath: used as a last-resort spawn when no Start marker exists
void find_spawn_cell(int* outTx, int* outTy);

// Load map.json (tile data + object layers) exported from Tiled using cJSON.
// Populates mapData, mapWidth/Height, tileWidth/Height, and the entity pool.
void load_map_json(const char* filepath);

// Render the tile map for the current camera frame.
// Uses map.png (one sub-image draw) when available; falls back to per-tile
// rendering from terrainSheet + mapData when map.png is missing.
void render_map_json(void);
