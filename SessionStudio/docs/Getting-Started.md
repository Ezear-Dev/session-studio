# Session Studio: getting started

Session Studio changes how your skater moves in **Session: Skate Sim**. You adjust a pose in the editor, you save, and the
game plays it right away, without restarting.

The game itself is never modified: everything goes through small clip files (`.sanim`). Only **your** skater changes.
Other players only see your clips in a SessionOpenMP session, when they have Session Anim Loader and the same clip files.

---

## 1. What to install

| Item | Where | Role |
|---|---|---|
| **UE4SS** | in `...\SessionGame\Binaries\Win64\` (file `UE4SS.dll`) | the mod loader. Without it, nothing loads. |
| **SessionAnimLoader** (Lua) | `...\Win64\Mods\SessionAnimLoader\` with `Scripts\main.lua` and `enabled.txt` | watches what the skater does. |
| **SessionAnimLoaderNative** (DLL) | `...\Win64\Mods\SessionAnimLoaderNative\` with `dlls\main.dll` and `enabled.txt` | adds your pose on top of the game's. |
| **The editor** | anywhere (the Desktop, for example) | `SessionStudio.exe` and its `docs` folder. |

ExpandedTricks is optional: when it is there, Session Anim Loader installs itself behind it.

**Check**: the first time you open the editor, the **Welcome** card lists your setup under "YOUR SETUP". Every line
should have a green dot; a red dot tells you in one sentence what to do. The card comes back with the **...** menu
(top right) > **Welcome and setup check**.

---

## 2. The two pages

The switch at the top left (or the **Tab** key) changes page:

- **Browse**: every move of the game, sorted by category on the left (with **My clips** and **Captures**). Each move is
  a card with small dots, one per stance and foot: **green** = the game's movement is recorded, an **orange centre** = a
  clip of yours plays there. Click a card to see its variants on the right. **Ctrl+K** searches.
- **Animate**: the 3D view, the **Inspector** card on its right, and the **timeline** at the bottom. The **Library**
  button at the top (key **B**) opens the list of moves over the 3D view, without leaving the page.

The bar at the top also holds **Save**, **Undo / Redo**, the **game pill** (what the game is doing, and what it did
with your last save: click it for the details) and the **...** menu.

---

## 3. First time in game

1. Start the game and put the skater on the board. The mod hooks in by itself after about 10 seconds.
2. The game then writes the skater's skeleton (`_skeleton.json`) into the clips folder: the editor needs it.
3. **Do the move you want to change once** (a kickflip, a manual, crouching…). The mod records its original movement
   by itself. In the editor, its dot turns **green** in Browse.

A move is done once **per stance** (Normal, Switch, Fakie, Nollie) and **per foot** (Regular, Goofy): they are different
movements in the game.

---

## 4. Your first clip

1. **Browse**: click the move, then the stance and the foot in the grid on the right, then **Animate this variant**
   (or double-click the card). The editor switches to Animate.
2. **3D view**: in blue, the ghost of the game's movement; on top, your skater with your changes (the bones your clip
   animates are green).
3. Pick a body part: click it in the view, or keys **1 to 7**. The tools at the top of the view:
   - **Pull (IK)**: drag the hand or the foot, the arm or the leg follows;
   - **Rotate**: rings turn the bone (key **I** switches between the two);
   - **Move**: the pelvis (key 5) raises, lowers or shifts the whole body, the feet stay in place.
4. With **Auto key** on (top of the timeline, next to Play), every gesture sets a **key** at the time shown on the timeline.
5. Move the playhead on the timeline to set more keys at other times. The clip moves smoothly from one key to the
   next. **Play** (or Space) to see the result; **Loop** (key L) replays it endlessly and **x0.5** plays it in slow
   motion, to study it.
6. **Save (Ctrl+S)**. The game pill at the top tells you whether the game reloaded the file.
7. Do the move again in game: you see your version.

**Undo / Redo** (Ctrl+Z / Ctrl+Y) undo any gesture. On every save, the previous version is copied to
`Documents\SessionStudio\backups` (**...** menu > **Open the backups**).

---

## 5. Good to know

- **Clip settings** (button at the top right of the 3D view, next to **Inspector**): the clip's name, mode, length,
  lock start and end, landing, copy to another variant.
- **Legs** can only be edited in trick and landing clips. On the ground, the feet must stay on the board.
- **Add / Replace** (Clip settings): "Add" sits on top of the game's movement and keeps its small variations; "Replace"
  forces exactly your pose.
- **Regular and goofy** (Clip settings): tick it so the clip also plays for the other foot, mirrored. Think of it
  before sharing a clip.
- **Copy the game's pose** ("Game" line of the timeline): pick a moment of the game's movement with the slider,
  then this button copies that pose to the playhead, on the picked part (or the whole body). Handy to put an arm
  back as in the game.
- **Lock start and end** (Clip settings, on for new trick and landing clips): the start and the end stay at the game's
  pose, so the clip joins the game without a jump. A trick's end is free when the trick has a landing clip (the
  landing takes over); a landing's start is free (it continues the trick). Padlocks on the timeline show the
  locked instants: pose between them. Untick it to pose them yourself.
- **Landing of this trick** (Clip settings): the landing after a trick, shown on the same timeline.
- **Copy / paste** (Ctrl+C / Ctrl+V): copies the selected keys, or your pose of the selected part, and pastes it at
  the playhead, inside the open clip.
- **Yellow warnings** (timeline and bottom left of the 3D view): a part is not back at the game's pose at the end of
  a trick or landing. Click to go there; the button next to it fixes it.
- **Your clips**: Browse > **My clips** lists them all, with an on / off switch. Right-click a clip (or its **...**
  button) to open, rename, duplicate or delete it. Delete moves the file to the backups.
- **In game**: **Ctrl+F3** turns the mod on or off, **Ctrl+F4** freezes the skater's current pose into
  `clips\captures` (Browse > **Captures**; opening one makes a new clip from it).
- **The game's replay** already contains your clips: the mod stands by during a replay.
- **Creator / Studio** (**...** menu, or **Preferences**, Ctrl+,): Studio mode shows precise values, the bone list and
  the advanced clip settings (stances, priority, blends, loop, drive).
- **Preferences** (Ctrl+,) also set the text size. **F1** lists every shortcut.

---

## 6. Sharing a clip

A clip is **a single** `.sanim` **file**, in `...\Win64\Mods\SessionAnimLoader\clips\`.

- To give it: send the file.
- To install one: copy it into that same folder. The game loads it within a second, even mid-session.

The person who receives it needs the mod installed (part 1), not the editor. The `_figures`, `_skeleton.json` and
`captures` files stay with you: they are only used by the editor.

---

## 7. Not supported yet

- Grinds and grabs: not detected reliably yet.
- Walking and running: in progress, with known issues.
- Powerslide: not checked yet.
- The editor shows a mannequin, not the skater's real body. An arm twisted or raised very high can crush the shoulder in
  game: the editor marks it in red, and the "Fix the shoulder" button helps.
- A game update can stop the mod from hooking in. It then refuses without breaking anything, and the game pill shows it.

---

## 8. When something goes wrong

| What you see | What to do |
|---|---|
| Red dot in the Welcome card | follow the sentence under that line (**...** menu > **Welcome and setup check**). |
| "Game animation not recorded yet" at the bottom of the 3D view | do the move once in game, in the right stance and with the right foot. |
| "REFUSED" in the game pill | click the pill: the reason is given under "Last save". |
| The game reloads the clip but nothing changes | is the mod on (game pill "mod on")? Ctrl+F3 in game. Is the clip on (switch in Browse)? |
| Another clip plays instead | Browse shows which clip plays on each variant; Clip settings: "Several clips on this variant" says which one wins. |

For a bug report: a screenshot of the editor, plus the `UE4SS.log` and `SessionAnimLoader.native.log` files from the `Win64`
folder.
