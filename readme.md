## TO-DO:
- Bugs to fix: Checkpoint flag appearing outside of map.
- Add full screen mode option.
- Add Wall-jump to character feature with asset "Wall Jump (32x32)". 
    - Expand level design with wall jump to reach the top platform.
    - Change finish line from bottom to top of the wall.


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