## How map assets is extracted from tiled editor into game.

#### 1. Base level design map was built in Tiled editor

- 16x16 tile images are used in Tiled editor to build the base level map.
- This map is exported as map.png (1x image export) for what the player SEES.
- This map is also exported as map.json for what the game KNOWS: tile grid (GIDs) + locations of enemies, traps, fruits, checkpoints, start/end points.


#### 2. How assets are imported (extract.c, the only file that touches the cJSON library)

- map.json stores numeric IDs (called GIDs, Global Tile IDs), one per tile cell — this file is the Tiled export, NOT a "cjson file". cJSON (`cJSON.c`) is just the C library used to read it.
- `load_map_json()` reads the whole file into `buffer` — a plain `char` array of raw JSON TEXT (letters, numbers, brackets). It contains no images.
- `cJSON_Parse(buffer)` converts that text into a cJSON tree in memory so fields can be looked up by name (`"width"`, `"tilewidth"`, `"layers"`, `"objects"`, ...).
- `process_layers_world()` walks the tree:
    - `"type": "tilelayer"` > its `"data"` GID array is copied into `mapData` (int array, owned by utils.c) — used ONLY for collision (`is_solid_px()`) and the fallback renderer.
    - `"type": "group"` (Trap / Enemies / Fruits / Checkpoints folders) > recurse into sub-layers.
    - `"type": "objectgroup"` leaf (e.g. `"name": "Kiwi"`) > `parse_object_group_entities()` maps the name to `EntityKind` and calls `spawn_entity()` once per `objects[]` point (`x`, `y` x entityScale 3.0).
- Each spawned entity loads its OWN art from `Assets/...` via its `ENTITY_DEFS` row (e.g. Kiwi.png, Saw strip) — GIDs are never "compared" to `terrain (16x16).png`. In the fallback renderer only, `tileId = GID - 1` picks which 16x16 cell to crop from the tilesheet. With map.png present, GIDs are not drawn at all.
- Cleanup: `cJSON_Delete(json)` frees the tree and `free(buffer)` frees the text — after extraction neither exists anymore. What persists is `mapData` + the filled `entityPool[72]`.


#### 3. How map.png (the visible level) is imported

- `map.png` (`Assets/Terrain/map.png`, 3200x1600 = 200x100 tiles at 16px) is loaded ONCE in `game_init()` via `CP_Image_Load()` into `mapImage` (the post-build `xcopy` copies `Assets/` next to the exe, so the path resolves at runtime).
- Every frame, `render_map_json()` (extract.c) draws it as ONE image: destination = full screen, source = the camera's sub-rectangle (`camX/scale`, `camY/scale`, size `SCREEN/scale`, with `scale = entityScale = 48/16 = 3.0` converting world px back to map px).
- So the split is: map.png = what you SEE (1 draw call), map.json/mapData = what is SOLID (collision probes) + where entities spawn.


#### 4. How assets get drawn:

- Enemies/fruits/traps/checkpoints come from step 2 above: one `spawn_entity()` call per Tiled point, seated onto tiles (feet on ground, bats tucked to ceilings), then driven per-frame by `update_entities()` (entity.c) and drawn by `render_entities()` (entity.c).
