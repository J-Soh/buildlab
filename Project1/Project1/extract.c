/*
@Copyright-credits
            Third-Party Assets, Libraries & Tools:
            - Logo: BuildLab logo created by the development team.
            - Visual Assets: "Pixel Adventure 1" (player, map level, enemies, traps)
              by Pixel Frog (https://pixelfrog-assets.itch.io/pixel-adventure-1)
            - Sound Effects: step.wav, jump.wav, respawn.wav, player_hit.wav,
              hit.wav, death.wav, coin.wav, button_click.wav
              Generated using Bfxr (https://www.bfxr.net/)
            - Background Music: Sound #595750 by szegvari
              (https://freesound.org/people/szegvari/sounds/595750/)
            - Menu Music: Sound #415805 by Sunsai
              (https://freesound.org/people/Sunsai/sounds/415805/)
            - External Libraries: cJSON (cJSON.c / cJSON.h) by Dave Gamble
              (MIT License)
            - Level Design Tool: Tiled Map Editor (https://www.mapeditor.org/)
*//*__________________________________________________________________________*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.c"
#include "cprocessing.h"
#include "utils.h"   // TILE_SIZE, SCREEN_WIDTH, mapData/mapWidth/mapHeight, is_solid_px
#include "game.h"    // mapImage, terrainSheet, sheetW, camX, camY (game-owned render state)
#include "extract.h" // own types, pool/table, load/render entry points

// EntityDef Structures are defined in extract.h file.
const EntityDef ENTITY_DEFS[ENT_COUNT] = {
    { "Assets/Traps/Saw/On (38x38).png",        NULL, NULL, NULL, NULL, 38, 38, 0.55f, 0.55f, true,  0.0f, 12.0f },
    { "Assets/Traps/Spikes/Idle.png",           NULL, NULL, NULL, NULL, 16, 16, 0.70f, 0.45f, true,  0.0f,  1.0f },
    { "Assets/Enemies/Chicken/Run (32x34).png", NULL, NULL, NULL, "Assets/Enemies/Chicken/Hit (32x34).png",
                                                                            32, 34, 0.68f, 0.68f, false, 110.0f, 10.0f },
    { "Assets/Enemies/Rino/Run (52x34).png",    NULL, NULL, NULL, "Assets/Enemies/Rino/Hit (52x34).png",
                                                                            52, 34, 0.68f, 0.68f, false, 130.0f,  8.0f },
    { "Assets/Enemies/Bat/Flying (46x30).png", NULL, "Assets/Enemies/Bat/Idle (46x30).png",
      "Assets/Enemies/Bat/Flying (46x30).png", "Assets/Enemies/Bat/Hit (46x30).png",
                                                                            46, 30, 0.60f, 0.60f, false, 150.0f, 12.0f },
    { "Assets/Items/Checkpoints/Start/Start (Idle).png", NULL, NULL, NULL, NULL,
                                                                            64, 64, 0.55f, 0.55f, false, 0.0f,  1.0f },
    { "Assets/Items/Checkpoints/Checkpoint/Checkpoint (No Flag).png",
      "Assets/Items/Checkpoints/Checkpoint/Checkpoint (Flag Idle)(64x64).png",
      NULL, NULL, NULL,
      64, 64, 0.55f, 0.55f, false, 0.0f, 1.0f },
    { "Assets/Items/Checkpoints/End/End (Idle).png", NULL, NULL, NULL, NULL,
                                                                            64, 64, 0.55f, 0.55f, false, 0.0f,  1.0f },
    { "Assets/Items/Fruits/Orange.png",     NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, 0.0f, 12.0f },
    { "Assets/Items/Fruits/Cherries.png",   NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, 0.0f, 12.0f },
    { "Assets/Items/Fruits/Kiwi.png",       NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, 0.0f, 12.0f },
    { "Assets/Items/Fruits/Melon.png",      NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, 0.0f, 12.0f },
    { "Assets/Items/Fruits/Strawberry.png", NULL, NULL, NULL, NULL, 32, 32, 0.65f, 0.65f, false, 0.0f, 12.0f },
};

Entity entityPool[MAX_ENTITIES]; // array of 72 slots (defined in line 158)
int entityCount = 0;        // How many used so far (start at 0).
float entityScale = 3.0f;   // world px per Tiled px == TILE_SIZE / tileWidth
int tileWidth = 16;
int tileHeight = 16;

// Map the Tiled object layer name to an EntityKind (mirrors fruits "extract orange")
static EntityKind kind_from_layer_name(const char* name) {
    if (!name) return ENT_COUNT;
    if (strstr(name, "Saw"))         return ENT_SAW;
    if (strstr(name, "Spikes"))      return ENT_SPIKES;
    if (strstr(name, "Chicken"))     return ENT_CHICKEN;
    if (strstr(name, "Rino"))        return ENT_RINO;
    if (strstr(name, "Bat"))         return ENT_BAT;
    if (strstr(name, "Start"))       return ENT_START;
    if (strstr(name, "Checkpoint"))  return ENT_CHECKPOINT;
    if (strstr(name, "End"))         return ENT_END;
    if (strstr(name, "Orange"))     return ENT_FRUIT_ORANGE;
    if (strstr(name, "Cherries"))   return ENT_FRUIT_CHERRIES;
    if (strstr(name, "Kiwi"))       return ENT_FRUIT_KIWI;
    if (strstr(name, "Melon"))      return ENT_FRUIT_MELON;
    if (strstr(name, "Strawberry")) return ENT_FRUIT_STRAWBERRY;
    return ENT_COUNT;
}

// Instantiate one map-placed object at world coords (map px * entityScale).
// Returns the pool slot, or NULL when full / kind invalid (so the caller can
// tweak per-object settings like Tiled facing).
static Entity* spawn_entity(EntityKind kind, float wx, float wy) {
    // return nothing if entityCount more than 72 (max pool size) or kind invalid.
    // entityCount is set to 0 initially (line 217)
    if (entityCount >= MAX_ENTITIES || kind >= ENT_COUNT) return NULL;

    // Look up kind's template (sprite path, cell size, hitbox, speed) from ENTITY_DEFS table
    // e.g. { "Assets/Items/Fruits/Kiwi.png",  NULL, 32, 32, 0.65f, 0.65f, false, true,  false,   0.0f, 12.0f },
    // if kind = ENT_FRUIT_KIWI, then &ENTITY_DEFS[ENT_FRUIT_KIWI] will read above comment.
    const EntityDef* def = &ENTITY_DEFS[kind];

    // point to empty index slot to store kind, alive or not, sprites, pos + respawn + hover baseline, hit box, movement, timers/flags 
    // e.g.  entityCount==0 → e = &entityPool[0], so slot 1 out of 72 is being used. Then entityCount +1 to move next empty slot.
    Entity* e = &entityPool[entityCount++];

    e->kind = kind; // current type (fruits, trap or animals)
    e->active = true; // true = alive (so update/draw/collision can process it)
    e->baseImage = def->path ? CP_Image_Load(def->path) : NULL; // load image's kind sprites (544 x 32 pixels (Kiwi) or 38x38 pixels (Saw))
    e->altImage = def->altPath ? CP_Image_Load(def->altPath) : NULL; // altImage only used by Checkpoint (NoFlag -> Flag)
    // Per-state strips (Chicken/Rino/Bat).
    e->idleImg = def->idlePath ? CP_Image_Load(def->idlePath) : NULL;
    e->runImg = def->runPath ? CP_Image_Load(def->runPath) : NULL;
    e->hitImg = def->hitPath ? CP_Image_Load(def->hitPath) : NULL;
    e->ceilingOutImg = (kind == ENT_BAT) ? CP_Image_Load("Assets/Enemies/Bat/Ceiling Out (46x30).png") : NULL;
    e->ceilingInImg = (kind == ENT_BAT) ? CP_Image_Load("Assets/Enemies/Bat/Ceiling In (46x30).png") : NULL;
    e->animState = (kind == ENT_BAT) ? 0 : 1; e->stateT = 0.0f; e->deathT = 0.0f;
    e->cellW = def->cellW; // weight of image (e.g. 32)
    e->cellH = def->cellH; // height of image (e.g. 32)
    e->totalFrames = 1;    // updates each frame by 1

    // All fruits images contains sprite. (e.g. Kiwi image contains 544 x 32 pixels)
    // Image sprite is a single image file that combines multiple smaller images.
    // We need to extract small images from image sprite.
    if (e->baseImage) { 
        int iw = CP_Image_GetWidth(e->baseImage); // 544 pixels
        if (iw > def->cellW) e->totalFrames = iw / def->cellW;
    }

    // Current pos = world px passed in (already x*3.0)
    e->x = wx; e->y = wy;
    e->sx = wx; e->sy = wy;

    // Hitbox size = sprite cell * 3.0 scale * fraction, e.g. Kiwi 32*3*0.65
    // The reason why add fraction is because PNG has transparent padding around it, 
    // so hitbox may trigger even when character did not "visually touch" trap or enemies.
    e->w = (float)def->cellW * entityScale * def->hbW;
    e->h = (float)def->cellH * entityScale * def->hbH;

    // Ground markers are authored as points ON the ground surface in Tiled:
    // lift the center so the sprite's feet rest at the point. Without this the
    // middle of the sprite sits at the point and half the checkpoint pole /
    // spike sinks under the tiles (see spike&cp.png).
    if (kind == ENT_START || kind == ENT_CHECKPOINT || kind == ENT_END || kind == ENT_SPIKES) {
        e->y -= (float)def->cellH * entityScale * 0.5f;  // seat feet at the point
        e->sy = e->y;
    }

    // Animals are plotted ~1 tile above the ground in Tiled: drop them so their
    // feet rest exactly on top of the first solid tile (no floating, no half-buried).
    // Needs mapData, so the Map tilelayer must come before Enemies in map.json layers.
    if (mapData && (kind == ENT_CHICKEN || kind == ENT_RINO)) {
        float halfH2 = e->h * 0.5f;
        float bottom = e->y + halfH2;
        if (is_solid_px(e->x, bottom)) {
            // Half-way inside a tile: pop straight up onto its top
            e->y = ((int)(bottom / TILE_SIZE)) * TILE_SIZE - halfH2 - 0.01f;
        } else {
            // Floating: scan down (max 3 tiles) and sit on the first solid top
            for (float d = 4.0f; d <= TILE_SIZE * 3.0f; d += 4.0f) {
                if (is_solid_px(e->x, bottom + d)) {
                    e->y = ((int)((bottom + d) / TILE_SIZE)) * TILE_SIZE - halfH2 - 0.01f;
                    break;
                }
            }
        }
        e->sy = e->y;
        e->vy = 0.0f;
        e->grounded = true;
    }

    // Patrol velocity: 
    // 1st ?: if speed>0 (Chicken 90, Rino 130), go to 2nd `?`. Else vx = 0.
    // 2nd ?: alternate -speed/+speed by even/odd count.
    // (Chicken/Rino facing can still be overridden per-object by Tiled object name
    // in parse_object_group_entities, and reset_run preserves it.)
    e->vx = (def->speed > 0.0f) ? (((entityCount & 1) == 0) ? -def->speed : def->speed) : 0.0f;

    // Start with zero velocity speed
    e->vy = 0.0f;

    // Face movement direction (1=right, -1=left for sprite mirror)
    e->facing = e->vx >= 0.0f ? 1 : -1;

    // Reset anim/hover timers and state flags
    // (seated animals above already start grounded = true)
    e->animT = 0.0f;
    if (kind != ENT_CHICKEN && kind != ENT_RINO) e->grounded = false;
    e->activated = false;
    return e;
}

// Extract objects of one objectgroup leaf (e.g. "Kiwi") into entity pool
static void parse_object_group_entities(cJSON* layer) {
    // Get "name" field from this layer dict, e.g. {"name":"Kiwi","type":"objectgroup","objects":[...]}
    // where "objects":[...] contains [{"x":1310,"y":867},{"x":1717,"y":889}...]
    cJSON* nameItem = cJSON_GetObjectItemCaseSensitive(layer, "name"); // nameItem = "Kiwi"

    // Safely extract C-string; if missing/non-string use "" to avoid NULL crash
    // e.g. return "" if nameItem = "".
    const char* name = (cJSON_IsString(nameItem) && nameItem->valuestring) ? nameItem->valuestring : "";

    // Map leaf name -> EntityKind via kind_from_layer_name()
    // e.g. "Kiwi"->ENT_FRUIT_KIWI, "Saw"->ENT_SAW,
    EntityKind kind = kind_from_layer_name(name);
    if (kind >= ENT_COUNT) return;   // unknown name returns ENT_COUNT -> skip (nothing to spawn)

    // Get "objects position" array from this leaf
    // e.g. Multiple Kiwi fruits have positions of [{"x":1310,"y":867},{"x":1717,"y":889}...]
    cJSON* objects = cJSON_GetObjectItemCaseSensitive(layer, "objects");
    if (!cJSON_IsArray(objects)) return;

    // Count how many kiwi fruits placed in tiled eidtor.
    // e.g. [{"x":1310,"y":867},{"x":1717,"y":889}] = 2 kiwi fruits.
    int n = cJSON_GetArraySize(objects);

    // For each Kiwi, get it's X & Y coordinates
    for (int i = 0; i < n; i++) {
        // Get i-th point dict, e.g. {"x":1310,"y":867,"point":true}
        cJSON* obj = cJSON_GetArrayItem(objects, i);
        if (!obj) continue;

        // Get object coordinates (Kiwi position at "x":1310,"y":867).
        cJSON* x = cJSON_GetObjectItemCaseSensitive(obj, "x");
        cJSON* y = cJSON_GetObjectItemCaseSensitive(obj, "y");

        // spawn one entity at world pos = Tiled px * 3.0 to match 16px->48px map; (or else fruits would spawn off scaled map)
        // size is scaled inside spawn_entity()
        if (cJSON_IsNumber(x) && cJSON_IsNumber(y)) {
            // Format: spawn_entity(kind, x position, y position);
            Entity* se = spawn_entity(kind, (float)x->valuedouble * entityScale, (float)y->valuedouble * entityScale);
            // Initial facing for Chicken/Rino comes from the OBJECT name in Tiled
            // (layer name "Chicken" picks the kind; point name picks facing):
            // select the point -> Properties -> Name -> "left" or "right".
            // Blank name = alternating default. This answers "which chicken faces where".
            if (se && (kind == ENT_CHICKEN || kind == ENT_RINO)) {
                cJSON* oname = cJSON_GetObjectItemCaseSensitive(obj, "name");
                const char* on = (cJSON_IsString(oname) && oname->valuestring) ? oname->valuestring : "";
                if (strstr(on, "left")) se->facing = -1;
                else if (strstr(on, "right")) se->facing = 1;
                se->vx = 0.0f;  // update loop drives vx from facing from now on
            }
        }
    }
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

                // Copy each GID into `mapData` buffer
                for (int i = 0; i < dataSize; i++) {
                    // Get GIDs tile number.
                    cJSON* tile = cJSON_GetArrayItem(data, i);
                    // Convert from JSON format to C lanuage format.
                    if (cJSON_IsNumber(tile)) mapData[i] = tile->valueint;
                }
            }
        }
        // objectgroup (contains fruits, chicken/rino enemies, checkpoints, and traps)
        else if (strcmp(type->valuestring, "objectgroup") == 0) {
            parse_object_group_entities(layer);
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

    // Create a buffer (that can hold entire map.json size):
    fseek(file, 0, SEEK_END); // moves file current position to file's end (to get total size of file)
    long length = ftell(file); // ftell =  gives the current position in bytes
    fseek(file, 0, SEEK_SET); // moves file end position back to start position.
    char* buffer = (char*)malloc((size_t)length + 1); // +1 to put null terminator `\0'` at the end of buffer.

    // Copies map.json file content into buffer
    // buffer now contains raw JSON text structure
    // buffer = // [ JSON ][ JSON ] ... [ JSON ][ ? ] (last byte is empty)
    fread(buffer, 1, length, file);
    buffer[length] = '\0'; // place `\0'` at the end of buffer. This indicates end of the C string.
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

    {  // per-kind breakdown so map points can be verified in the console (CP_Engine_Run(1); to show below printf message)
        int counts[ENT_COUNT] = { 0 };
        for (int i = 0; i < entityCount; i++)
            if (entityPool[i].kind < ENT_COUNT) counts[entityPool[i].kind]++;
        printf("Entities: saw %d, spikes %d, chicken %d, rino %d, bat %d, start %d, checkpoint %d, end %d, orange %d, cherries %d, kiwi %d, melon %d, strawberry %d\n",
            counts[ENT_SAW], counts[ENT_SPIKES], counts[ENT_CHICKEN], counts[ENT_RINO], counts[ENT_BAT],
            counts[ENT_START], counts[ENT_CHECKPOINT], counts[ENT_END], counts[ENT_FRUIT_ORANGE], counts[ENT_FRUIT_CHERRIES],
            counts[ENT_FRUIT_KIWI], counts[ENT_FRUIT_MELON], counts[ENT_FRUIT_STRAWBERRY]);
    }
    printf("Map loaded: %dx%d, tile %dx%d, entities %d\n", mapWidth, mapHeight, tileWidth, tileHeight, entityCount);
}

// Renders the tilemap parsed from map.json using Terrain (16x16).png
void render_map_json(void) {
    CP_Settings_ImageMode(CP_POSITION_CORNER);

    // Pre-rendered map (.png): 1 draw of the camera's sub-rect. 
    // map.png is in original pixel. Camera position is 3 times size of original pixel. So need convert camera back to original pixel.
    // In game.c coordinates: cam at 1 pixel = enlarged gameplay pixel (x3), we divide by 3 to get "original" camera tiled pixel pos.
    // map.png is a 1x Tiled export (3200x1600), so map-px = world-px / entityScale.
    // mapData/map.json is still loaded and used for collision only.
    if (mapImage) {
        float s = (entityScale > 0.0f) ? entityScale : 3.0f; // s = entityScale = 3
        // Converts the camera position from world coordinates (900 pixel) into map.png coordinates (300 map-image pixels)
        float sx0 = camX / s, sy0 = camY / s;
        // Converts pixel-wide world viewport to original map pixel
        float sw = SCREEN_WIDTH / s, sh = SCREEN_HEIGHT / s;
        CP_Image_DrawSubImage(mapImage, 0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT,
                              sx0, sy0, sx0 + sw, sy0 + sh, 255);
        return;
    }

    // Fallback: per-tile render from the tilesheet map.json (only if map.png missing):
    // if (!mapData || !terrainSheet) return;
    // int tilesPerRowInSheet = (int)(sheetW / tileWidth);
    // if (tilesPerRowInSheet < 1) tilesPerRowInSheet = 1;

    // for (int y = 0; y < mapHeight; y++) {
    //     for (int x = 0; x < mapWidth; x++) {
    //         int tileIndex = y * mapWidth + x;
    //         int tileGid = mapData[tileIndex];

    //         if (tileGid <= 0) continue;

    //         float screenX = x * TILE_SIZE - camX;
    //         float screenY = y * TILE_SIZE - camY;

    //         if (screenX < -TILE_SIZE || screenX > SCREEN_WIDTH ||
    //             screenY < -TILE_SIZE || screenY > SCREEN_HEIGHT) continue;

    //         int tileId = tileGid - 1;
    //         float cropX = (float)((tileId % tilesPerRowInSheet) * tileWidth);
    //         float cropY = (float)((tileId / tilesPerRowInSheet) * tileHeight);

    //         CP_Image_DrawSubImage(terrainSheet, screenX, screenY, TILE_SIZE, TILE_SIZE, cropX, cropY, cropX + (float)tileWidth, cropY + (float)tileHeight, 255);
    //     }
    // }
}
