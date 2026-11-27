# Session Studio: reference for creators

This is the detailed reference. For a first clip, read `Getting-Started.md`.
Version: mod (SessionAnimLoaderNative) v0.24, clip format `sanim` version 1, Session: Skate Sim Steam build of November 2026.

---

## How it works

1. **The Lua half** (`Mods\SessionAnimLoader`) reads the skater's animation state every frame (manual, trick popped, stance,
   on foot, crouch, speed…) and passes it to the DLL.
2. **The DLL** (`Mods\SessionAnimLoaderNative`) hooks the skater mesh's final bone pass. On top of the game's finished pose, it
   adds the clips whose trigger matches. It also records the game's own movement of each move the first time you do it
   (`clips\_figures\*.json`), plus the skeleton (`clips\_skeleton.json`).
3. **The editor** shows those recordings as a skeleton and writes `.sanim` clips into the clips folder. The DLL rescans
   that folder every second and reloads a file whose content changed.

Only your own skater is changed. In a SessionOpenMP session, players who have Session Anim Loader and the same clip files
also see your clips (v0.19, 2-player test pending).

---

## Moves (states) and variants

A clip targets one or more **states** and, for each, **stances** and **feet**.

| State (`trigger.states`) | Editor name | Notes |
|---|---|---|
| `Trick` | flip tricks (Browse > Tricks) | `trigger.trick` = the game's trick name (`KickFlip`, `Treflip`…); empty = every flip trick |
| `Manual`, `NoseManual` | Manual, Nose manual | loop while held |
| `OnFoot` | On foot (standing) | off the board, slow |
| `Walking`, `Running` | Walking, Running | off the board, by speed. **Work in progress** |
| `Riding` | Riding (standing) | on the board, on the ground, nothing else going on |
| `Pushing` | Pushing | a push; falls back to the Riding clip when it has none |
| `Crouch` | Crouch | on the board, crouched; the only ground state with Fakie / Nollie |
| `Landing` | Landing | From the moment a trick returns to the ground (or a landing without trick): 0.8 s, or as long as the landing clip, up to 2 s. Past 0.8 s, pushing, crouching, a new trick or any other move ends it. A landing clip may name its trick (`trigger.trick`): it then plays only after that trick, in that trick's stance, and wins over the generic Landing clip |
| `Dismount` | — | used through the Riding clip's dismount part (below) |
| `Powerslide` | Powerslide | **not checked yet** |
| `Grind`, `Grab` | hidden | **not detected reliably**: grinds share one recording, grabs are untested |

**Stances**: `Normal`, `Switch`, `Fakie`, `Nollie` (Fakie / Nollie only for tricks and Crouch).
**Feet**: `regular` (left foot forward), `goofy` (right foot forward), or `any`.

Each (move, stance, feet) is a separate recording, named `<Move>_<RGS|SWS|FKS|NLS>_<regular|goofy>`, done once in game.
The editor's **record again** button asks the DLL to record a move again the next time you do it.

---

## Which clip plays

Every frame, among the enabled clips whose state, stance and feet match:

1. During a trick, clips of that trick (or of every trick) come first.
2. Then the highest `trigger.priority` wins.
3. Then the most specific one: a named trick > feet set > fewer stances.
4. Then file order.

Browse shows the clip that plays on each variant (variant detail, on the right). **Clip settings** lists the competing
clips under **Several clips on this variant**.

Between two ground poses, the change is cross-faded for at least the size of the pose gap (up to 0.8 s). A trick
clip's legs are given back to the game after the catch; a landing clip writes the legs again.

**Trick clips follow the jump** (2026-11-25). The game lets you pop higher or lower. A trick clip keeps the instants
of the game's own events in its recording (`play.markers`: takeoff, catch, landing preparation, ground). In game,
the pop, the flip and the catch play at real speed; the hang time (catch -> landing preparation, yellow on the
timeline) and the last part stretch or shrink so that the clip ends exactly at the ground contact, predicted from
the skater's vertical speed. The landing is a clip of its own (button **Landing of this trick**), edited over the
trick's recorded ground tail and started at the real contact.

---

## The `.sanim` file

UTF-8 JSON. Unknown top-level fields are kept as they are (the editor writes them back); a wrong value in a known field
makes the game refuse the file, with the reason in `SessionAnimLoader.native.log`. Times are in seconds, angles are quaternions `[x, y, z, w]`
in each bone's local space, the pelvis offset is in cm in component space (Z up).

```json
{
  "format": "sanim",
  "version": 1,
  "name": "KickFlip_RGS_regular",
  "enabled": true,
  "trigger": {
    "states": ["Trick"],
    "trick": "KickFlip",
    "stances": ["normal"],
    "feet": "regular",
    "priority": 0
  },
  "play": { "drive": "time", "duration": 0.63, "loop": false },
  "blend": { "in": 0.25, "out": 0.3 },
  "mode": "additive",
  "mirrorForGoofy": false,
  "tracks": { "AMXX_R_UpperArm": [ { "t": 0, "q": [0, 0, 0, 1], "interp": "smooth" } ] },
  "weights": { "AMXX_R_UpperArm": 1 },
  "pelvis": [ { "t": 0, "offset": [0, 0, -10], "interp": "smooth" } ]
}
```

| Field | Values | Meaning |
|---|---|---|
| `mode` | `additive` / `replace` | add the key rotation to the game's pose, or force it |
| `mirrorForGoofy` | bool | with `feet: any`, mirror left / right for the other foot |
| `play.drive` | `time` / `ManualRatio` / `cycle` | clip clock: seconds, the game's manual balance, or the walking stride |
| `play.loop` | bool | loop (ground states only; tricks never loop) |
| `play.hold` | bool | a single pose all the time (held pose) |
| `play.ratioRange` | `[a, b]` | ManualRatio range mapped to the clip (drive `ManualRatio`) |
| `play.markers` | `{air, catch, land, ground}` s | trick clips: the game's events in the recording (written by the editor) |
| `play.landing`, `play.groundAt` | s | obsolete (lot 5 landing part): read, ignored by the player, split by the converter |
| `play.dismount` | s | riding clips: the part played once when the feet leave the board |
| `keys[].interp` | `smooth` / `linear` / `step` | curve to the next key |
| `weights` | 0..1 per bone | how much of the track applies |
| `lockEnds` | bool | editor only (the game ignores it): trick and landing clips keep their start / end at the game's pose |

`_studio.json` in the clips folder holds two expert settings: `mirrorConv` (per-bone mirror convention overrides) and
`legPostPass` (re-solve the legs after other mods, on by default).

---

## Editor layout

- **Top bar**: Browse / Animate switch (Tab), **Library** drawer (B, Animate only), what is edited, **Save** (Ctrl+S),
  **Undo / Redo**, the **game pill** (game and mod state, result of the last save; click for details) and the **...**
  menu (new, open, save as, Studio mode, Preferences, shortcuts, welcome and setup check, guide, folders).
- **Browse**: collections on the left (every trick and ground state by category, **My clips**, **Captures**; search
  Ctrl+K; filters **Recorded** and **Has clips**), one card per move with a dot per stance x feet (green = recorded,
  orange centre = one of your clips is on, white ring = the variant being edited), the variant detail on the right
  (variant grid, which clip plays in game, its clips with an on / off switch, **Animate this variant**, **New clip for
  this variant**). **My clips** is a sortable table with filters; right-click a clip for open, rename, duplicate, delete.
- **Animate**: the 3D view with its floating tools (top: Rotate, Pull (IK), Move, local / world rings; top right:
  **Inspector**, **Clip settings**, view options; bottom right: camera), the Inspector card on its right, the timeline
  at the bottom (transport, Auto key, phase ribbon, "Game" row, tracks). Messages about the view sit at the bottom left.
- **Preferences** (Ctrl+,): Creator / Studio mode, text size, clips folder, backups, welcome at start.
  **Keyboard shortcuts** (F1): every key and mouse gesture.

## Editor tools

- **Pull (IK)** on hand, elbow, shoulder, foot, knee; "chest follows the hand"; elbow / knee swivel slider.
- **Rotate** with local or world rings; precise pitch / yaw / roll in Studio mode.
- **Pelvis**: offset with the feet kept planted; the legs bend or stretch (limited to their reach).
- **Shoulder check**: the game mesh has no twist bones; red arm = risk of a crushed shoulder; **Fix the shoulder** moves
  part of the raise to the clavicle and part of the twist to the wrist, hand kept in place.
- **Mirror** a bone's pose to the other side; **Copy to another variant** (left / right swapped when needed).
- **Timeline**: groups by limb (unfold for single bones), select / Shift / box select, drag keys, delete, interpolation,
  zoom (Ctrl + wheel, or wheel on the ruler), pan (Shift + wheel, middle drag), Home / End, frame step (arrows), 30 fps.
  Editor playback: **Loop** (L; editor only, the clip file is unchanged) and speed x1 / x0.5 / x0.25.
  **Empty rows too** also lists the bones without keys.
- **Copy the game's pose**: copies the recorded game pose at the "Game" slider time to the playhead, for the picked
  bone and its children (or every editable bone); a key right away with Auto key on.
- **Lock start and end** (Add mode trick and landing clips): locked instants are keyed at the game's pose after every
  edit. Trick: start locked, end locked unless the trick has an enabled landing clip for all its variants (legs never
  locked at a trick's end). Landing: end locked, start free. A trick end posed beyond 3 degrees / 1 cm when the trick
  loses its landing cover is kept (warning) until it is brought back. Changing the length moves the locked end key.
- **Copy / paste** inside the open clip: a pose (your change, keyed like a normal key) or keys (spacing and
  interpolation kept, snapped to frames, keys past the end skipped).
- **End warnings**: parts more than 3 degrees / 1 cm from the game's pose at a trick or landing end, or a landing
  start different from its trick's end. Legs are ignored at a trick's end.
- **Backups**: every save copies the previous version to `Documents\SessionStudio\backups\<clip>\` (20 kept per clip).
  Deleting a clip (Browse > My clips, or the Library drawer) moves the file there too.

---

## Logs

| File (in `Win64`) | What it tells |
|---|---|
| `SessionAnimLoader.native.log` | DLL version, install state (`installation: …`, hooked or refused), every clip (re)load or refusal with its reason, recordings |
| `UE4SS.log` (lines `[SessionAnimLoader]`) | the Lua side: states seen, tricks recognised, keys |

The editor reads `SessionAnimLoader.native.log` to show the game state and the reload result of your last save.

---

## Limits

- Grinds, grabs: not detected reliably. Walking / running: work in progress. Powerslide: not checked.
- The hook depends on this game build: after a game update the DLL may refuse to install (logged, nothing breaks).
- The editor shows bones, not the skinned body.
- Other players see your clips only with Session Anim Loader and identical clip files.

## Licence

SessionAnimLoaderNative is GPL-3.0 (it reuses the UE4SS ABI header of SessionOpenMP and ExpandedTricks' hook method).
