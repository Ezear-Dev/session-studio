# AnimStudio: getting started

AnimStudio changes how your skater moves in **Session: Skate Sim**. You adjust a pose in the editor, you save, and the
game plays it right away, without restarting.

The game itself is never modified: everything goes through small clip files (`.sanim`). Only **your** skater changes.
Other players only see your clips in a SessionOpenMP session, when they have AnimStudio and the same clip files.

---

## 1. What to install

| Item | Where | Role |
|---|---|---|
| **UE4SS** | in `...\SessionGame\Binaries\Win64\` (file `UE4SS.dll`) | the mod loader. Without it, nothing loads. |
| **AnimStudio** (Lua) | `...\Win64\Mods\AnimStudio\` with `Scripts\main.lua` and `enabled.txt` | watches what the skater does. |
| **AnimStudioNative** (DLL) | `...\Win64\Mods\AnimStudioNative\` with `dlls\main.dll` and `enabled.txt` | adds your pose on top of the game's. |
| **The editor** | anywhere (the Desktop, for example) | `AnimStudioEditor.exe` and its `docs` folder. |

ExpandedTricks is optional: when it is there, AnimStudio installs itself behind it.

**Check**: open the editor and click **Help** at the top. Every line of the "Installation" part should have a green dot.
A red dot tells you in one sentence what to do.

---

## 2. First time in game

1. Start the game and put the skater on the board. The studio hooks in by itself after about 10 seconds.
2. The game then writes the skater's skeleton (`_skeleton.json`) into the clips folder: the editor needs it.
3. **Do the move you want to change once** (a kickflip, a manual, crouching…). The studio records its original movement
   by itself. In the editor, the move turns **green** in the Library.

A move is done once **per stance** (Normal, Switch, Fakie, Nollie) and **per foot** (Regular, Goofy): they are different
movements in the game.

---

## 3. Your first clip

1. **Library** (left): pick the stance and the foot at the top, then the move in the **Tricks** tab.
2. **3D view** (centre): in blue, the ghost of the game's movement; on top, your skater with your changes (the bones your clip animates are green).
3. Pick a body part with the buttons above the view (or keys **1 to 7**):
   - **Pull (IK)**: drag the hand or the foot, the arm or the leg follows;
   - **Rotate**: rings turn the bone;
   - **Pelvis**: raise, lower or shift the whole body, the feet stay in place.
4. With **Auto key: ON** (top), every gesture sets a **key** at the time shown on the timeline (bottom).
5. Move the red playhead on the timeline to set more keys at other times. The clip moves smoothly from one key to the
   next. **Play** (or Space) to see the result;
   **Loop** (key L) replays it endlessly and **x0.5** plays it in slow motion, to study it.
6. **Save (Ctrl+S)**. The bottom bar tells you whether the game reloaded the file.
7. Do the move again in game: you see your version.

**Undo / Redo** (Ctrl+Z / Ctrl+Y) undo any gesture. On every save, the previous version is copied to
`Documents\AnimStudio\backups` (**Backups** button at the bottom).

---

## 4. Good to know

- **Legs** can only be edited in trick and landing clips. On the ground, the feet must stay on the board.
- **Add / Replace** (Clip tab): "Add" sits on top of the game's movement and keeps its small variations; "Replace"
  forces exactly your pose.
- **Regular and goofy** (Clip tab): tick it so the clip also plays for the other foot, mirrored. Think of it before
  sharing a clip.
- **Copy the game's pose** ("Game" line of the timeline): pick a moment of the game's movement with the slider,
  then this button copies that pose to the playhead, on the picked part (or the whole body). Handy to put an arm
  back as in the game.
- **In game**: **Ctrl+F3** turns the studio on or off, **Ctrl+F4** freezes the skater's current pose into
  `clips\captures` (**Captures** tab of the Library).
- **The game's replay** already contains your clips: the studio stands by during a replay.
- **Creator / Studio** (top of the Inspector, right): Studio mode shows precise values and the **Advanced** tab
  (stances, priority, blends, loop, drive).

---

## 5. Sharing a clip

A clip is **a single** `.sanim` **file**, in `...\Win64\Mods\AnimStudio\clips\`.

- To give it: send the file.
- To install one: copy it into that same folder. The game loads it within a second, even mid-session.

The person who receives it needs the mod installed (part 1), not the editor. The `_figures`, `_skeleton.json` and
`captures` files stay with you: they are only used by the editor.

---

## 6. Not supported yet

- Grinds and grabs: not detected reliably yet.
- Walking and running: in progress, with known issues.
- Powerslide: not checked yet.
- The editor shows a skeleton, not the skater's body. An arm twisted or raised very high can crush the shoulder in game:
  the editor marks it in red, and the "Fix the shoulder" button helps.
- A game update can stop the studio from hooking in. It then refuses without breaking anything, and the editor shows it
  at the bottom left.

---

## 7. When something goes wrong

| What you see | What to do |
|---|---|
| Red dot in **Help** | follow the sentence under that line. |
| "Game animation: to do in game" | do the move once in game, in the right stance and with the right foot. |
| "REFUSED by the game" at the bottom | hover it: the reason is given. |
| The game reloads the clip but nothing changes | is the studio on (green dot at the bottom left)? Ctrl+F3 in game. Is the clip "On in game"? |
| Another clip plays instead | Clip tab: the "Several clips on this variant" list says which one wins. |

For a bug report: a screenshot of the editor, plus the `UE4SS.log` and `AnimStudio.native.log` files from the `Win64`
folder.
