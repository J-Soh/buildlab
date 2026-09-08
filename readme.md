## TO-DO:
- Bugs to fix: Checkpoint flag appearing outside of map.


## Infomation:
- `Project1` folder contains game dev project.

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
1. Save & Commit both branches (`git add .` > `git commit -m "edited"`)
2. Compare & Merge 2 file branches.
```
git merge char-backup
```