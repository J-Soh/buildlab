- **Parses** = to read its raw text or binary data and convert it into a structured, machine-readable format that a program can easily understand and use.
- **`tileSheet`** = a `.png` image that contains each `16x16` pixels for designing levels in Tile Editor.
- `map` = a `.json` file that contains the completed level designed in Tile Editor (represented in GID (Global Tile IDs)).
- In tile editor, layer are rendered in this order: **bottom to top**.
---

- After creating map in Tile Editor > File > export as `map.json`.
- Bring both `map.json` and `map.png (Tileset)` into Assets folder. Then follow Guide below.
## Guide
- `map.json` is exported from external editor software (**Tiled**) and stores numeric IDs (called **GIDs (Global Tile IDs)**). representing each tile.
	- **GIDs (numbers)** tell you which tile from the tilesheet to use.
- The `terrain (16x16).png` image contains  many `16×16` tile images.
- The code extracts `map.json` and parses into a buffer.
- The code then connects this `terrain (16x16).png` IDs into each tile of this buffer.
	- In other words, paste each corresponding image tile from `terrain (16x16).png` into this `map.json` buffer based on it's GID.

1. Load `tileSheet` (`terrain (16x16).png`), and get it's height and width.
```c 
static CP_Image tileSheet = NULL;
static float sheetW = 0.0f; // Width of Terrain (16x16).png
static float sheetH = 0.0f; // Height of Terrain (16x16).png

void game_init(void) {
	tileSheet = CP_Image_Load("Assets/Terrain/Terrain (16x16).png");
	// Get image dimensions:
	sheetW = (float)CP_Image_GetWidth(tileSheet);  // 352 pixels 
	sheetH = (float)CP_Image_GetHeight(tileSheet); // 176 pixels 
}
```
   
2. Load the `map.json`, and parse each GIDs into buffer `mapData`.
```c fold:'Load map.json into mapData buffer'
static int mapWidth = 0;
static int mapHeight = 0;
static int tileWidth = 16;
static int tileHeight = 16;
static int* mapData = NULL;

static void load_map_json(const char* filepath) {
	// rb = read binary. This gets exactly as they are in the original file
	FILE* file = fopen(filepath, "rb"); 
	
	// Create a buffer (that can hold entire map.json size):
	fseek(file, 0, SEEK_END); // moves file current position to file's end.
	long length = ftell(file); // ftell =  gives the current position in bytes
	fseek(file, 0, SEEK_SET); // moves file end position back to start position.
	char* buffer = (char*)malloc((size_t)length + 1); // +1 to put `\0'` at the end of buffer.
	
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
	
	// Get map layer (Grid map structure built using `tileSheet` + obj location points)
	cJSON* layers = cJSON_GetObjectItemCaseSensitive(json, "layers");

    if (cJSON_IsArray(layers)) {
    	// Find number of layers (1 layer = map Structure, 2nd layer = fruit objects location)
        int layerCount = cJSON_GetArraySize(layers);
        
        for (int l = 0; l < layerCount; l++) {
	        // (1st loop) layers[0] -> 1st item. (2nd loop) layers[1] -> 2nd item.
            cJSON* layer = cJSON_GetArrayItem(layers, l);
            
            if (layer) {
	            // "type": "tilelayer" or "type": "objectgroup"
                cJSON* type = cJSON_GetObjectItemCaseSensitive(layer, "type");
                
                // Get map structure:
                // type (layer) exist, contains string, and string == "tilelayer"
                if (type && type->valuestring && strcmp(type->valuestring, "tilelayer") == 0) {
	                // Get map structure data (GIDs).
                    cJSON* data = cJSON_GetObjectItemCaseSensitive(layer, "data");
                    if (cJSON_IsArray(data)) {
                        int dataSize = cJSON_GetArraySize(data); // No. of GIDs
                        if (mapData) free(mapData);
                        mapData = (int*)malloc(sizeof(int) * dataSize); // buffer
							
						// Copy each GID into `mapData` buffer
                        for (int i = 0; i < dataSize; i++) {
	                        // Get GIDs tile number.
                            cJSON* tile = cJSON_GetArrayItem(data, i);
                            if (cJSON_IsNumber(tile)) {
	                            // Convert from JSON format to C lanuage format.
                                mapData[i] = tile->valueint;
                            }
                        }
                        break; // Stop after finding tile layer
                    }
                }
            }
        }
    }

    cJSON_Delete(json);
    printf("Map loaded: %dx%d, tile %dx%d\n", mapWidth, mapHeight, tileWidth, tileHeight);
}

void game_init(void) {
	load_map_json("Assets/Terrain/map.json");
}
```
-  **parse** a file means to read its raw text or binary data and convert it into a structured, machine-readable format that a program can easily understand and use.
- (line 38): object location points refer to fruits, enemies, etc that are indicated with location points on the map.
#### Structure
```md
Open map.json (line 1)
     ↓
Read JSON text (line 4)
     ↓
Parse JSON into buffer (line 15)
     ↓
Find map dimensions / tile layer / GIDs (line 25-28 (json-format), 33-36 (c-format))
     ↓
Store GIDs in mapData (line 28-31)
     ↓
Later, use each GID to draw the correct 16×16 tile
```

3. Renders the map layout by using each GID in `mapData` to select and draw the corresponding tile from `Terrain (16x16).png`.
```c
#define TILE_SIZE 48.0f // Scaled tile size for rendering

// Renders the map layout by using each GID in `mapData` to select and draw the corresponding tile from `Terrain (16x16).png`.
// mapData = contains GIDs for each tile
// tileSheet = Terrain (16x16).png
static void render_map_json(void) {
	// Stop function if mapData or tileSheet contains nothing.
    if (!mapData || !tileSheet) return;
	
	// Start at top left corner
    CP_Settings_ImageMode(CP_POSITION_CORNER);
    int tilesPerRowInSheet = (int)(sheetW / tileWidth); // 352 / 16 = 22 tiles per row
    if (tilesPerRowInSheet < 1) tilesPerRowInSheet = 1; // prevent division/module by 0 ltr on. 
    
    // Move from left to right per row. 
    for (int y = 0; y < mapHeight; y++) { // mapHeight = 100
        for (int x = 0; x < mapWidth; x++) { // mapWidth = 200
            int tileIndex = y * mapWidth + x;  // 0*100+x → 0,1,2,3,4...
            int tileGid = mapData[tileIndex]; // retrive GIDs for index 0,1,2,3,4....

			// GID of `0` = empty tile. So skip current loop.
            if (tileGid <= 0) continue;
	
			// Converts map coordinate to screen coordinate
			// Shift the entire world relative to what the player sees.
            float screenX = x * TILE_SIZE - camX;
            float screenY = y * TILE_SIZE - camY;
            
			// Skip tiles outside of screen resolution
            if (screenX < -TILE_SIZE || screenX > SCREEN_WIDTH ||
                screenY < -TILE_SIZE || screenY > SCREEN_HEIGHT) continue;
			
			// Convert GID into tile index:
			// tilesheet uses 0-based indexing. But tileGid uses 1-based indexing. So minius by 1.
            int tileId = tileGid - 1;
            
            // Find the tile's X & Y position in the tilesheet
            float cropX = (float)((tileId % tilesPerRowInSheet) * tileWidth); // tileld % 22 * 16
            float cropY = (float)((tileId / tilesPerRowInSheet) * tileHeight); // tileld / 22 * 16

            CP_Image_DrawSubImage(tileSheet, screenX, screenY, TILE_SIZE, TILE_SIZE, cropX, cropY, cropX + (float)tileWidth, cropY + (float)tileHeight, 255);
        }
    }
}

void game_update(void) {
	render_map_json();
}
```
- (line 33): why `tileGid - 1` is because my `tilesheet` use **0-based indexing**. But `tileGid` uses 1-based indexing.
- (line 26 & 27): If you include `camX` and `camY`. The map structure near center of screen. 
	- If you remove `camX` and `camY`, Then the map structure will be at different location (maybe top left).

| `GID` | `tileId` |
| --- | --- |
| `1` | → `0` |
| `2` | → `1` |
| `3` | → `2` |
| `4` | → `3` |