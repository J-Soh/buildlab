## TO-DO:
- Check for any memory leak:
```To check for memory leak in Visual Studio, add the following include to your main.c/cpp file:
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

and this line at the top of your entry point function: 
_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
```
#### Code To-Do:
- Transfer all "collision" code (in game.c) into a function block that resides in "utils.c" file.


---

Copyright info:
- buildlab logo created by us.
- Sounds (step.wav, jump.wav, respawn.wav, player_hit.wav, hit.wav, death.wav, coin.wav, button_click.wav) created from https://www.bfxr.net/
- Background music: https://freesound.org/people/szegvari/sounds/595750/, menu music: https://freesound.org/people/Sunsai/sounds/415805/
- Game visual asserts (player, map level, enemies, trap design) from https://kenney.nl/assets
- External libraries used: cJSON.c file
- External graphic tool used: Tiled editor

## Current features of the game:
- `Project1` folder contains game dev project.
- Enemies (Rino, chicken)
    - When enemies sees player in front of them, their speed increase and move towards him.
    - If an enemy hits player, decrease health by 1.
    - If player jumps on top of them, enemy is defeated.
- Controls:
    - Space to jump.
    - A & D to move left and right.

- How level design map is produced:
    - Map was pre-rendered with map.png exported from tile editor.
    - Enemies (Rino, chicken, Bat), traps (saw), checkpoints are marked in tile editor. So this is exported as map.json, which is then extracted into the game coordinates for each items.

---

### How to run game:
- In VS > File > Open > Project/Solution > click `Project1`.
- `Ctrl + shift + B` > `F5`.


---

## How to use Git:

#### How to push to server:
```c
git add .
git commit -m "message"
git push .
```

#### To see all branches : 
```
git branch
```
- If it shows `*` beside the git branch, that means your currently in this branch.

#### To create branch: 
```
git checkout -b (name)
```
- The `-b` refers to "create" branch.
- Without `-b` means to switch to that branch.

_Example:_

- Backup: `git checkout -b backup`.
- If working on game character movement: `git checkout -b char-movement`.


#### How to switch different branches:
- To go main branch: `git checkout main`
- To go other branches: `git checkout backup` 

#### How to compare and merge 2 file branches:
- If you want to transfer code from main to backup branch:
```c
git checkout main 
git add .
git commit -m "merge" 

git checkout backup 
git merge main //Transfer code from main to backup branch
```
- If you want to transfer code from backup to main branch:
```c
git checkout backup 
git add .
git commit -m "merge" 

git checkout main 
git merge backup //Transfer code from backup to main branch
```

#### To delete a branch:
- Delete the local branch
```
git branch -d (name) 
```
- Delete the remote branch
```
git push origin --delete (name)
```