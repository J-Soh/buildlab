update_entities():
update_entities() moves every living object once per frame (called from game_update()).
Stomped enemies (deathT above 0.0f) freeze, count down, then deactivate.
Enemies flashing Hit (animState 2) freeze for 0.5 seconds, then go back to run/fly.
Bat sleeps on the ceiling (Idle). When player comes within BAT_AGGRO_RADIUS = 344.0f (8 tiles), it plays Ceiling In alert, then chases player. When player leaves (1.5x radius), it flies back to its perch and sleeps again.
Plant faces the player and shoots one seed per cycle (fires at atkT 0.35 seconds, ends attack at 0.67 seconds, rests 1.6 seconds). It only attacks when player is within 560 left/right and 220 up/down.
Chicken and Rino always walk left and right (reverse at walls and edges). When player is in front of them (within 480 left/right and 110 up/down), they charge at 2.5x speed.
Enemies fall with ENT_GRAVITY and land on solid tiles (is_solid_px()), which sets grounded to true. Only grounded chicken/rino can walk or charge.

---
check_player_entity_collisions():
check_player_entity_collisions() checks what the player touches once per frame (called from game_update()).
Checkpoint and End gates work even when player just got hit (ignores invulnT). Checkpoint saves respawn position and plays coin sound. End sets levelComplete to true.
Fruits: disappear on touch, play Collected sparkle (spawn_pickup_fx()) and coin sound.
Traps (hazard): call hurt_player() on touch.
Enemies: if player falls fast (vy above 80) and lands on head (within 16 pixels above enemy top), player bounces (vy = -470) and enemy plays Hit then deactivates (deathT = 0.45 seconds). Otherwise enemy flashes Hit and calls hurt_player().
Enemies already playing Hit (deathT above 0.0f) are skipped so one stomp cannot hit twice.

---
player_update():
player_update() allows player to move left or right (A/D) and jump (SPACE). Called once per frame from game_update().
Whenever player jumps, a jump sound (CP_Sound_PlayAdvanced()) and dust particles (spawn_dust()) play.
Player can double jump.
player.jumps = 0: player on ground (reset on landing).
player.jumps = 1: player jumped once.
player.jumps = 2: player jumped twice (no more jumps until landing).
player.buffer is set to 0.15f on every SPACE press, so a press up to 0.15 seconds BEFORE landing still jumps on touchdown.
COYOTE_TIME = 0.10f: lets player jump up to 0.10 seconds AFTER running off an edge (grounded just turned false, coyote still above 0.0f).



Footstep dust spawns every 0.30 seconds while running on ground.

Player gets knockback when hit: hurt_player() sets lockT = 0.5f and kbx = -300 x facing (away from the enemy). While lockT is above 0.0f, input is ignored and vx = kbx, which decays (kbx -= kbx x 8 x dt) so the shove eases out.



doubleAnimT = double-jump animation clock. doubleAnim latches true when the double jump fires; render_player() uses doubleAnimT x 12 for the flip frame (held at frame 5). It unlatches (false) on landing or stomp-bounce.

Falling: vy += GRAVITY x dt pulls player down (GRAVITY = 2800, JUMP_VEL = -980 gives about 3.5 tiles of jump height). MAX_FALL = 1150 caps fall speed so long falls stay controllable and cannot tunnel through thin tiles.
