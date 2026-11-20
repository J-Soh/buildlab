## What each file does:

`main.c`: main entry of the game, and will go into menu.c file

`menu.c`: shows digipen & buildlab logo at the start, then goes into main menu of the game. 
- workflow: start at `menu_update()` > `update_splash()` (wait 2 seconds for each splash logo to appear) > `draw_splash()` (draws logo at start) > `draw_background()`, play music and `draw_menu_ui()`
    - Design: main menu is drawn with rectangle shape (`draw_menu_ui()` > `draw_cream_panel()`).
- It consist of buttons: volume, play, credit and controls.
    - `handle_menu_input()`: It checks for user that interacts with each button.
        - `is_mouse_over()` from `utils.c` is used to check if mouse is over a button.
    - Once user press space-bar or click “play” button on menu.c, it will enter into `game.c` which is the gameplay.
    - If user wants to exit (ESC key), a design rectangular shape card comes up (`draw_quit_confirm()`) for confirmation to quit.


`game.c`: the main gameplay where player must reach the finishing line to beat the game. It owns the game loop + HUD + lifecycle, and calls into the other .c files:
- workflow: start at `game_init()` (load gameplay art, `load_map_json()` from `extract.c`, find Start pos) > `game_update()` per frame > `game_exit()` (free everything + NULLs).
    - Design: game screen is layered draws — `draw_background()` > `render_map_json()` > `render_entities()` > `draw_bullets()` > `render_player()` > hearts HUD.
- `game_update()`: ESC routing (pause menu / cancel popup) > `player_update()` from `player.c` > camera follow + fell-out-of-world rescue > `update_entities()` + `check_player_entity_collisions()` + `update_bullets()` from `entity.c` > render.
    - Pause menu itself (`draw_pause_menu()`, `handle_pause_input()`) lives in `pause.c`; the loop only reads/writes the `paused`/`pausePage` flags.

`extract.c`: the only file that touches `cJSON.c` — it turns `map.json` (Tiled export) into the tile grid + entity pool.
    - Design: `ENTITY_DEFS` table (sprite path, cell size, hitbox fraction, speed per kind) + `entityPool[72]`; `spawn_entity()` seats each spawn (checkpoints lifted, animals dropped to tiles, bats tucked to ceilings).
- workflow: `load_map_json()` (file > buffer > `cJSON_Parse`, dims, per-kind console breakdown) > `process_layers_world()` (tilelayer `data[]` > `mapData`; recurse `group` folders) > `parse_object_group_entities()` (leaf `name` > `EntityKind` > spawn each `objects[]` point).
    - `render_map_json()`: draws `map.png` as one camera sub-rect (per-tile loop is fallback if png missing); `mapData` itself is only read for collision.

`player.c`: the player mechanic (move, jump, take damage, draw).
- Design: `Player` struct + tuning defines + `player` state live here.
- workflow: `player_update()` per frame (A/D input > jump/double jump + coyote/buffer > gravity > timers > `player_physics()` > footsteps) > `render_player()` (ground shadow blob + Idle/Run/Jump/Double/Fall select).
    - `player_physics()`: axis-separated AABB move + collide vs solid tiles (walls snap, feet land, ceiling bumps) using `is_solid_px()` from `utils.c`.
    - `hurt_player()`: iframe-gated damage + knockback; death (hearts 0) calls `reset_run()` in `game.c`. This is the player half of the player<->enemy seam.

`entity.c`: enemies (bat, chicken, rino, plant), fruits, plant seeds + the enemy half of the seam.
    - Design: static world objects (traps, checkpoints) ride the same pool; behavior reads the pool that `extract.c` fills.
- workflow per frame: `update_entities()` (bat sleeper: perch > Ceiling-In alert > chase > return; plant shooter faces player and fires seeds into `bullets[]`; chicken/rino patrol + 2.5x charge when player seen; gravity + wall/ledge checks) > `check_player_entity_collisions()` > `update_bullets()` > `render_entities()` + `draw_bullets()`.
    - `check_player_entity_collisions()`: checkpoint/end gates (sets respawn/levelComplete) > fruit pickup (+ sparkle + coin) > stomp test (`vy > 80` + lands on head = bounce + Hit strip) else `hurt_player()`. Enemy art is feet-anchored so legs rest on tiles.

`utils.c`: shared canvas + generic helpers every file uses (no gameplay of its own).
- Design: fixed 1600x900 numbers used as-is; single-instance globals (`screenW/H`, `mapData`, `hearts`, sounds, FX pools) with `extern`s in `utils.h`.
- It holds: scrolling `draw_background()` + `draw_cream_panel()` cards, volume/mute/click + `is_mouse_over()` hit-tests, tile solidity (`tile_is_bg_fill()` > `is_solid_cell()` > `is_solid_px()`), box overlap (`aabb_overlap()`), dust/pickup pools + spawners, and frame-crop draws (`draw_sprite_frame()`, `draw_sheet_frame()`).
- `pause.c`: pause menu overlay (dim + banner + RESUME/RESTART/OPTIONS rows, OPTIONS audio page, MENU/QUIT panel).
    - Design: same cream-card look as the main menu; the game loop freezes while `paused` is true.
- workflow: ESC in `game.c` toggles `paused`/`pausePage` > `draw_pause_menu()` + `handle_pause_input()` per frame.
    - Destructive buttons (RESTART, MENU, QUIT) only arm `draw_confirm_popup()` (QUIT GAME? / RESTART RUN? / BACK TO MENU?); YES runs the action, NO (or ESC) cancels.

---

 
`entity.c`:
- render_entities(): (enemies + fruits draw; body moved verbatim).
- update_entities():(enemy AI + enemy physics; body moved verbatim).
- update_bullets(): (plant seeds are enemy weapons; body moved verbatim).
 
`extract.c`: (map render lives with map data)
- load_map_json, render_map_json, process_layers_world, parse_object_group_entities
 
