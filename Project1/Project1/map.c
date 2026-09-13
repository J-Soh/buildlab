// map.c
// Tile-map globals, collision queries, JSON loading, and camera rendering.
#include "map.h"
#include "entity.h"
#include "cJSON.h"

// ---- Map tile data ----
int  mapWidth = 0;
int  mapHeight = 0;
int  tileWidth = 16;
int  tileHeight = 16;
int* mapData = NULL;

// ---- Tilesheet handle (fallback renderer when map.png is missing) ----
CP_Image terrainSheet = NULL;
float    sheetW = 384.0f;
float    sheetH = 192.0f;

// ---- Pre-rendered map image ----
CP_Image mapImage = NULL;

// ---- Camera ----
float camX = 0.0f;
float camY = 0.0f;

// ---- Scale factor (world px per Tiled px) ----
float entityScale = 3.0f;

// GIDs that look solid but are background fill (transparent to physics)
bool tile_is_bg_fill(int gid) {
    return gid == TILE_GID_BG_FILL_1 || gid == TILE_GID_BG_FILL_2;
}

// True if the tile CELL is solid (bounds-aware). gid 0 and navy fill pass through.
bool is_solid_cell(int tx, int ty) {
    if (tx < 0 || tx >= mapWidth || ty >= mapHeight) return true;  // walls + floor
    if (ty < 0 || !mapData) return false;                          // open sky
    int gid = mapData[ty * mapWidth + tx];
    if (gid <= 0 || tile_is_bg_fill(gid)) return false;
    return true;
}

bool is_solid_px(float wx, float wy) {
    int tx = (int)floorf(wx / TILE_SIZE);  // floorf: negatives map to -1, not 0
    int ty = (int)floorf(wy / TILE_SIZE);
    return is_solid_cell(tx, ty);
}

// Leftmost lowest open pocket with headroom + floor: adapts to map edits
void find_spawn_cell(int* outTx, int* outTy) {
    if (!mapData || mapWidth <= 0 || mapHeight <= 0) { *outTx = 0; *outTy = 0; return; }
    for (int tx = 0; tx < mapWidth; tx++) {
        for (int ty = mapHeight - 1; ty >= 1; ty--) {
            int i = ty * mapWidth + tx;
            if (mapData[i] == 0 && mapData[(ty - 1) * mapWidth + tx] == 0 && is_solid_cell(tx, ty + 1)) {
                *outTx = tx; *outTy = ty;
                return;
            }
        }
    }
    *outTx = mapWidth / 2; *outTy = mapHeight / 2;
}

// Get map tile layer (Grid map structure built by `tileSheet` + obj location points groups)
static void process_layers_world(cJSON* layers) {
    if (!cJSON_IsArray(layers)) return;

    // Find number of layers (n) (1 layer = map Structure, 2nd layer = fruit objects location)
    int n = cJSON_GetArraySize(layers);
    for (int l = 0; l < n; l++) {
        // (1st loop) layers[0] -> 1st item. (2nd loop) layers[1] -> 2nd item.
        cJSON* layer = cJSON_GetArrayItem(layers, l);
        if (!layer) continue;

        // "type": "tilelayer" (refers to main map design) or "type": "objectgroup" (refers to object props in map)
        cJSON* type = cJSON_GetObjectItemCaseSensitive(layer, "type");

        // skip loop if type (layer) does not exist or type contains no string 
        if (!cJSON_IsString(type) || !type->valuestring) continue;
        
        // Get map structure:
        // string == "tilelayer" (map design level)
        // strcmp(A, B) == 0 means True.
        if (strcmp(type->valuestring, "tilelayer") == 0) {
            cJSON* data = cJSON_GetObjectItemCaseSensitive(layer, "data");
            if (cJSON_IsArray(data)) {
                int dataSize = cJSON_GetArraySize(data); // No. of GIDs
                if (mapData) free(mapData);
                mapData = (int*)malloc(sizeof(int) * dataSize); // buffer
                if (mapData == NULL) {
                    // if Memory allocation failed, return nothing.
                    return;
                }

                // Copy each GID into `mapData` buffer
                for (int i = 0; i < dataSize; i++) {
                    // Get GIDs tile number.
                    cJSON* tile = cJSON_GetArrayItem(data, i);
                    // Convert from JSON format to C language format.
                    if (cJSON_IsNumber(tile)) mapData[i] = tile->valueint;
                }
            }
        }
        // objectgroup (contains fruits (kiwi, melon etc), enemies (plant, chicken etc), checkpoints (start, end)), trap (saw, spikes)
        else if (strcmp(type->valuestring, "objectgroup") == 0) {
            process_layers_world_entities(layer);
        }
        // group contain Folders of Fruit, Enemies, Trap, Checkpoint.
        else if (strcmp(type->valuestring, "group") == 0) {
            cJSON* sub = cJSON_GetObjectItemCaseSensitive(layer, "layers");
            if (cJSON_IsArray(sub)) process_layers_world(sub);
        }
    }
}

// Loads and parses map.json exported from Tiled using cJSON
void load_map_json(const char* filepath) {
    FILE* file = fopen(filepath, "rb"); //default path is at OutDir/Assets/Terrain/map.json
    if (!file) return;

    // Create a buffer (that can hold entire map.json size):
    fseek(file, 0, SEEK_END); // moves file current position to file's end (to get total size of file)
    long length = ftell(file); // ftell = gives the current position in bytes
    fseek(file, 0, SEEK_SET); // moves file end position back to start position.
    char* buffer = (char*)malloc((size_t)length + 1); // +1 to put null terminator `\0` at the end of buffer.

    // Copies map.json file content into buffer
    // buffer now contains raw JSON text structure
    fread(buffer, 1, length, file);
    buffer[length] = '\0'; // place `\0` at the end of buffer. This indicates end of the C string.
    fclose(file);

    // cJSON take & understand this raw JSON text structure so that you can obtain dimensions (width, height) etc.
    cJSON* json = cJSON_Parse(buffer);
    free(buffer);
    if (!json) return;

    // Get map dimensions
    cJSON* width = cJSON_GetObjectItemCaseSensitive(json, "width"); //"width": 200 
    cJSON* height = cJSON_GetObjectItemCaseSensitive(json, "height"); //"height": 100
    cJSON* tilew = cJSON_GetObjectItemCaseSensitive(json, "tilewidth"); //"tilewidth": 16
    cJSON* tileh = cJSON_GetObjectItemCaseSensitive(json, "tileheight"); //"tileheight": 16

    // Sometimes after getting dimensions (width etc) does not guarantee a number.
    // Thus, we do `(cJSON_IsNumber(width))` to check if it is a number.
    // We convert JSON format `"width": 200` to C format `mapWidth = 200`
    if (cJSON_IsNumber(width)) mapWidth = width->valueint;
    if (cJSON_IsNumber(height)) mapHeight = height->valueint;
    if (cJSON_IsNumber(tilew)) tileWidth = tilew->valueint;
    if (cJSON_IsNumber(tileh)) tileHeight = tileh->valueint;

    cJSON* layers = cJSON_GetObjectItemCaseSensitive(json, "layers");
    if (cJSON_IsArray(layers)) {
        process_layers_world(layers);   // tile layer + objectgroups (incl. nested groups)
    }

    cJSON_Delete(json);

    {  // per-kind breakdown so map points can be verified in the console
        int counts[ENT_COUNT] = { 0 };
        for (int i = 0; i < entityCount; i++)
            if (entityPool[i].kind < ENT_COUNT) counts[entityPool[i].kind]++;
        printf("Entities: saw %d, spikes %d, plant %d, chicken %d, bat %d, rino %d, start %d, checkpoint %d, end %d, orange %d, cherries %d, kiwi %d, melon %d, strawberry %d\n",
            counts[ENT_SAW], counts[ENT_SPIKES], counts[ENT_PLANT], counts[ENT_CHICKEN], counts[ENT_BAT], counts[ENT_RINO],
            counts[ENT_START], counts[ENT_CHECKPOINT], counts[ENT_END], counts[ENT_FRUIT_ORANGE], counts[ENT_FRUIT_CHERRIES],
            counts[ENT_FRUIT_KIWI], counts[ENT_FRUIT_MELON], counts[ENT_FRUIT_STRAWBERRY]);
    }
    printf("Map loaded: %dx%d, tile %dx%d, entities %d\n", mapWidth, mapHeight, tileWidth, tileHeight, entityCount);
}

// Renders the tilemap parsed from map.json using Terrain (16x16).png
void render_map_json(void) {
    CP_Settings_ImageMode(CP_POSITION_CORNER);

    // Pre-rendered map (like PixelAdventure's map.png): 1 draw of the camera's
    // sub-rect. map.png is a 1x Tiled export (3200x1600), so map-px = world-px / entityScale.
    // mapData/map.json is still loaded and used for collision only.
    if (mapImage) {
        float s = (entityScale > 0.0f) ? entityScale : 3.0f;
        float sx0 = camX / s, sy0 = camY / s;
        float sw = screenW / s, sh = screenH / s;
        CP_Image_DrawSubImage(mapImage, 0.0f, 0.0f, screenW, screenH,
                              sx0, sy0, sx0 + sw, sy0 + sh, 255);
        return;
    }

    // Fallback: per-tile render from the tilesheet map.json (only if map.png missing)
    if (!mapData || !terrainSheet) return;
    int tilesPerRowInSheet = (int)(sheetW / tileWidth);
    if (tilesPerRowInSheet < 1) tilesPerRowInSheet = 1;

    for (int y = 0; y < mapHeight; y++) {
        for (int x = 0; x < mapWidth; x++) {
            int tileIndex = y * mapWidth + x;
            int tileGid = mapData[tileIndex];

            if (tileGid <= 0) continue;

            float screenX = x * TILE_SIZE - camX;
            float screenY = y * TILE_SIZE - camY;

            if (screenX < -TILE_SIZE || screenX > screenW ||
                screenY < -TILE_SIZE || screenY > screenH) continue;

            int tileId = tileGid - 1;
            float cropX = (float)((tileId % tilesPerRowInSheet) * tileWidth);
            float cropY = (float)((tileId / tilesPerRowInSheet) * tileHeight);

            CP_Image_DrawSubImage(terrainSheet, screenX, screenY, TILE_SIZE, TILE_SIZE, cropX, cropY, cropX + (float)tileWidth, cropY + (float)tileHeight, 255);
        }
    }
}
