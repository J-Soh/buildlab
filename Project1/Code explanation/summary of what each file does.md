### menu.c:
- DigiPen and Buildlab splash screens
- Main menu UI
- Play button and SPACE key
- Volume button
- Credits
- Skin selection
- Quit confirmation

### game.c loads:
- Player images
- Enemy and item images
- Sounds and music
- Background and fonts
- The Tiled game level map from `Assets/Terrain/map.json`
- It also resets the player and game state.

```
game_update()
 ├── Read ESC / pause input
 ├── Update player movement
 ├── Update particles
 ├── Move camera
 ├── Update enemies
 ├── Check collisions
 ├── Update checkpoint quizzes
 └── Draw everything
```

### extract.c:
- It uses `cJSON.c` and `cJSON.h` to:
  - Open `map.json`
  - Read the JSON text
  - Parse map width, height, and tile size
  - Copy tile data into mapData
  - Read object layers
  - Convert object names into entities such as:
    - Chicken
    - Chicken
    - Rhino
    - Checkpoint
    - End point
    - Fruits
    - Traps
- The map is drawn using `map.png`, while the JSON tile data is mainly used for collision detection.

### player.c:
- A/D movement
- Jumping
- Double jump
- Gravity
- Tile collision
- Coyote time and jump buffering
- Damage and knockback
- Player animation
- Footstep effects

### entity.c:
- Chicken and Rhino movement
- Traps
- Fruits
- Checkpoints
- End gate
- Enemy/player collisions
- Stomping enemies
- The entity definitions and shared entity pool & Preprocessor macro are declared in extract.h.

### utils.c:
- Tile solidity checks
- Mouse hit testing
- Background scrolling
- UI panels
- Audio volume control
- AABB collision checks
- Dust particles
- Pickup effects
- Score popups
- Shared global game state
- Its declarations & Preprocessor macro are in utils.h.

### pause.c
- Resume  
- Restart 
- Options 
- Skin picker  
- Return to menu  
- Quit  
- Confirmation popups 
- The world stops updating while paused  

### skin.c 
- skin.c loads and displays four player skins
- skin.h contains the shared declarations
- The selected skin is shared between the menu and gameplay

### quiz.c:
- Player reaches a checkpoint
- A multiplication question appears
- Correct answer allows a buff choice
- Wrong answer gives no buff
- The world is frozen while the overlay is open
- Available buffs include:
  - Fortitude
  - Swift Feet
---