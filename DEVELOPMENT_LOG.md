# DEVELOPMENT_LOG.md — synthetic history

Important steps, technical decisions, problems and their solutions. Current state: `PROJECT_STATUS.md`; rules:
`CLAUDE.md`.

**Sources.** Git holds only 9 commits (2026-11-26, from `df13bbb` "AnimStudio: animation studio mod" to `7610976`
"Rename"); everything before was developed outside Git. The earlier history below comes from the local working logs:
`_archive/docs/studies/REPRISE.md` (French log, 2026-11-22 to 26), `notes/HANDOFF.md`, and the plans and ledgers in
`_archive/docs/plans-and-designs/`. Version numbers are the DLL's `kVersion` (shown as v0.N). Dates are 2026.

## Timeline

| Date | Version | Step |
|---|---|---|
| 11-22 | — | Feasibility study: **hybrid** approach (C++ runtime in game + external editor, JSON files reloaded hot) |
| 11-22 | — | P0 Lua probe: UE 4.27, 70 bones, 132 triggers catalogued. P1 toolchain (VS 2022 already present) |
| 11-22 | v0.1 | **P2 runtime proof**: our pose layer visible in game, no crash, compatible with ExpandedTricks and SessionTweaks |
| 11-23 | v0.5 | **P3 clip player** (`.sanim`, additive / replace, fades, per-bone goofy mirror, hot reload, capture) — 7 in-game tests OK |
| 11-23 | v0.6 | **P4 external editor** (Dear ImGui + ImGuizmo); DLL records the game's moves ("figures") and the skeleton — 10 tests OK |
| 11-23 | v0.6 | P5a hand IK + per-bone timeline (validated); figures recorded for exactly 3 s |
| 11-23 | v0.7 | P5b first flip trick (Kickflip) and editable legs; P5c hold keys and elbow / shoulder / knee handles |
| 11-24 | v0.8 | **P6**: every flip trick, variants per stance × feet, leg post pass, spline keys, Add mode by default |
| 11-25 | — | Lot 2: limb bar, chest follows the hand, **pelvis offset** (format field `pelvis`) |
| 11-25 | v0.9-v0.10 | Lot 4 base states (OnFoot, Crouch, Landing, Riding, then Walking, Pushing); held pose; replay stand-by |
| 11-25 | v0.11 | Step-cycle drive (`drive: cycle`) for walking |
| 11-25 | v0.12 | Lot 5: cross-fades between clips, base-clip debounce, timeline grouped by limb, step interpolation |
| 11-25 | v0.13-v0.14 | Running state; dismount part of the riding clip; distance-based fades; shoulder check / fix in the editor |
| 11-25 | editor | **UI redesign phases 1-5**: resizable panels + backups, Library tabs, Stage toolbar + Inspector (Creator / Studio), timeline zoom / pan, English UI option, welcome window and guides |
| 11-25 | v0.15 | Named grinds attempted (failed: name empty offline); walking / running parked by the user |
| 11-25/26 | v0.16-v0.17 | **Landing sync**: trick clips follow the real jump; landing becomes its own clip; learned correction per trick |
| 11-26 | v0.18-v0.19 | **Multiplayer** through SessionOpenMP: network probe, then real clip sync (2-player test OK) |
| 11-26 | v0.20-v0.22 | Smoothed receiver clock; clips identified by content; `clips\others\` |
| 11-26 | — | Sources translated to **English only** (French UI option removed); probe C0 live preview: **no-go**, removed |
| 11-26 | v0.23 | Landing state up to 2 s; editor copy / paste, end-pose warnings, lock start and end |
| 11-26 | editor | Solid **mannequin** in the 3D view (six looks until accepted), optional approximate board |
| 11-26 | v0.24 | **Rename**: AnimStudio → Session Studio (editor) + Session Anim Loader (mod); installed and checked in game |
| 11-26 | editor | **UX/UI redesign started** on branch `redesign-ui`: design v1 (restyle of the 4-panel layout) rejected, v2 approved (Browse / Animate workspaces); step 0 splits `app.cpp` into panel files without behaviour change |
| 11-27 | editor | `redesign-ui` (steps 0-8c) merged into `main` by fast-forward and pushed to GitHub at the owner's request; `test.cmd` ALL PASS (18667) just before. Steps 7-8c were still "user check pending" |
- **2026-11-27, official first release (v0.28):** the owner declared the prepared package the official release and asked for commit and push. Before the commit: no personal data found by pattern scan in the changes, and the source zip identical to the working tree for every file it ships. The GitHub release page, the tag and the repository visibility were not touched.
- **2026-11-27, v0.28 published:** the owner created the GitHub release on the public repository (tag `v0.28` on `c45108b`, which adds a README heading change made after the source zip was built). Public API check: the three attached files match `release\out` by size and SHA-256.

## Key technical decisions

- **Hybrid architecture (11-22).** An in-game editor was rejected: a C++ runtime layers clips on the game's pose, a
  separate editor writes files. The mod works without the editor.
- **Where to write the pose (P2).** The vtable slot `FinalizeBoneTransform` of the skater mesh, writing the
  component-space buffer after the game's animation. Installed **after** ExpandedTricksNative, which refuses a slot
  that is not the engine function.
- **Pure logic apart from the game (P3).** `src/` has no UE4SS dependency and is tested offline; `main.cpp` is the only
  game-facing file. The editor later reused the same library (`asn_editor` links `asn_core`).
- **C++ allowed for this project** (the owner had refused it for an earlier mod, ManualTurnRate).
- **Per-bone mirror (P3)**: each bone's mirror convention is derived from the reference positions, overridable in
  `_studio.json`.
- **Editor view mirrored on Y (P4)** because Unreal is left-handed.
- **Variants (P6)**: one game recording per (move, stance, feet); clips target variants; mirroring only when the
  authored body differs from the skater's body.
- **Additive by default (P6)** when the game animation of the variant is recorded.
- **Smooth keys = C1 cubic Hermite spline (11-24)**, replacing a per-segment ease that stopped at every key.
- **Landing as its own clip (11-25)**: the lot 5 "landing part inside the trick clip" was removed in favour of
  markers in the trick clip + a separate landing clip started at the real contact (`play.landing/groundAt` still parsed
  for the converter, ignored by the player).
- **Multiplayer without changing SessionOpenMP (11-26)**: send which clip, time, weights (~15 Hz) on its public mod
  channel; each PC plays its own copy of the clip on the peer's skater. Clips identified by **content**, not file name.
- **English everywhere (11-25/26)**: chat in French, project in English; the editor's French option was removed.
- **No design rounds (11-25)**: short plan, then code (owner's rule).
- **Mannequin built by code (11-26)**: rigid parts per bone; a CC0 skinned model was offered and declined for now.
- **Redesign = two workspaces, not a restyle (11-26)**: the owner rejected a re-skin of the four fixed panels (3D view
  too small, inspector too big, library a cramped list, weak timeline, overloaded top bar). Approved direction:
  *Browse* (full-screen library: collections, trick cards with stance × feet matrix, detail panel) and *Animate*
  (stage and timeline full width, floating tools on the stage edges so the gizmo area stays free, contextual inspector
  card, clip settings panel, trick phase ribbon in the timeline). Still ImGui 1.93 without docking, own layout code.
- **Step 0: `app.cpp` split by panel (11-26)**: same `App` class over `app*.cpp`; the file-local helpers became
  `inline` in `app_internal.h` (namespace `app_detail`, pulled in with `using namespace`), so no line of logic changed.
- **Step 1: one theme (11-26)**: `app/ui_theme` holds the palette (dark anthracite, skate orange `#FF6B1A` for what is
  active or primary, blue `#5B8CFF` for what comes from the game), the ImGui style, the fonts and the icons;
  `app/ui_widgets` holds the themed buttons (hover fade), toggles, icon buttons, segmented control, switch, record
  toggle, chip and status dot. Fonts and icons come from `C:\Windows\Fonts` (Segoe UI, Segoe UI Semibold, Segoe Fluent
  Icons or Segoe MDL2 Assets): nothing shipped, no licence question; without the icon font, buttons show text.
  The old colour names (`kAccent`...) now point at the theme, so every panel changed colour without logic changes.
- **Step 2: workspaces and a light top bar (11-27)**: `ws_` (Browse / Animate, saved `ui.workspace`) and `Lay()`
  rectangles per workspace; the status strip became the game pill (short state, worst colour wins) and its popup
  (game, mod, last save, skeleton, folders, last messages); `Status()` messages also show as a toast. New, Open,
  Save as, Studio mode, help and folders moved to the ... menu; Auto key to the timeline transport. Tab switches the
  workspace: the shell claims the Tab key (`SetKeyOwner`) so ImGui's keyboard navigation cannot focus a button with
  it (Space would then press that button instead of playing). Editing keys are ignored in Browse.
- **Step 3: floating stage tools (11-27)**: the tools are ImGui *child windows* of the stage window. ImGuizmo only
  reacts when the stage window itself is hovered (`IsHoveringWindow` in ImGuizmo.cpp), so a ring under a card can
  never be grabbed through it, and the stage's own picking / orbit use `IsWindowHovered()` the same way. Cards are
  placed by a corner from their size at the last frame. The Studio bone list moved from the Inspector to the body
  picker ("All bones"). The mannequin shader got a warm wrapped key, cool fill and rim, warm floor bounce and a soft
  highlight curve; `--selftest` now compiles the stage shaders (they are compiled at start: an error would silently
  fall back to the skeleton view).
- **The editor's floor is not the game's ground in tricks (11-27)**: the user saw the feet go through the floor. Measured
  on the real recordings: in component space the body goes down to -41 cm (KickFlip), -30 (Ollie), -16 (Landing),
  while standing / riding stay at or above 0. A first fix put the floor under the lowest point of the game animation;
  the skater then floated 41 cm up at the start. A second measure showed the ground reference IS 0 (root bones and the
  board at 0, feet ~11 cm up on the board): only a few frames of the pop dip to -40 cm. Decision (user): the floor is an
  option (View > Floor, off by default, always at 0); orientation axes in a corner show camera turns. Reset frames the
  posed skater (`FrameBox`); the body picker was removed at the user's request.
- **Step 4: the Inspector becomes a card (11-27)**: no panel column; the card is a child window of the stage placed
  in a strip that the 3D canvas leaves free on its right (camera, picking and ImGuizmo use the narrower canvas), so
  the skater stays centred in the visible part without an off-centre projection. The clip settings (and, in Studio,
  the advanced ones) open in the same card from the timeline; the Inspector tabs and their tab-forcing code are gone.
  After the user's check: "Clip settings" moved next to "Inspector" on the view; card buttons lighter, fields darker.
- **Step 5: timeline header and ribbon (11-27)**: three control lines became one (keys left, transport and time centre,
  warnings and view right, groups placed from their width at the last frame); the end warnings became a chip with a
  popup; a phase ribbon under the ruler names the parts of the trick (the hang time is the part that stretches in
  game); rows carry the limb colours of the stage. The row and key interactions were not touched.
- **Landing on the trick's timeline, two files (11-27)**: the user asked for the trick and its landing on one timeline,
  then for one file. One file would have reverted the validated landing sync (the mod starts the landing clip at the
  real ground contact) and changed the mod and the format, so the choice was one timeline over two files: the open
  part is `doc_`, the other waits in `comp_` and they are exchanged when the playhead crosses the ground contact
  (`app_landing.cpp`); shared-time arithmetic in `model/two_part_time` (tested). The landing is locked by default
  (the game's); unlocked, it is the landing clip of that trick, written by Save with the trick.
- **Redesign step 6, the Browse page (11-27)**: Browse is now its own page (`app_browse.cpp`) instead of the old
  Library panel, which stays as the drawer of Animate. The card data (variants, recorded, clip counts) and the rule
  "which clip plays on this variant" moved from `App::ClipTargets` to `model/library` with offline tests; the playing
  clip follows the player order (enabled, higher priority, then more specific, then the first). "New clip for this
  variant" goes through the usual confirmations (`Ask::ChangeFigure`, then `Ask::New` via `newAfterPick_`).
- **Redesign step 7, dialogs (11-27)**: `BeginDialog` / `DialogButtons` / `EndDialog` (`app_dialogs.cpp`) give every
  modal the same look and keys (Enter = main button unless it is a Danger one, Esc = the last button). The welcome
  stays a plain window, not a modal: the "Clips folder" dialog it opens must come on top of it. New Preferences
  (text size = `style.FontScaleMain`, saved "ui.textsize") and a full keyboard and mouse list (F1).
- **Redesign step 8, polish and guides (11-27)**: the stage messages start right of the Library drawer when it is open.
  Both guides now describe the two pages. Pose thumbnails on the Browse cards were left out: they need an offscreen
  render of each recording, too big for a polish step.
- **Library drawer like Browse (11-27, user request)**: the drawer of Animate now uses the Browse pieces. The variant
  grid and the list of cards moved into `App::VariantGrid` / `App::LibraryCards` (`app_browse.cpp`), used by both; the
  drawer is a plain window without title bar (own header and close button), and the tab bar became `ui::Segmented`
  (the "restore the saved tab over several frames" trick of the ImGui tab bar is gone with it). Its Tricks list then
  took the Browse organisation: the card drawing moved to `App::LibCardUi` (one card, used by both), the collection is
  a combo over Browse's own `browseCat_` and the filters are Browse's (one choice for both places).
- **Open the active clip (11-27, user report)**: picking KickFlip opened the first KickFlip file of the list, not the
  one turned on. `Ask::ChangeFigure` now opens `ClipToOpen` (`model/library`): the clip `PlayingClip` says the game
  plays, else the first clip made for that variant, else a new clip. Tested in `test_library.cpp`.

- **Real board recording, step 1: probe (11-27)**: scope chosen by the user: record the game's visible board and
  show it in the editor, read-only (no board keys, no `.sanim` change). First a throwaway **Lua-only** probe
  (`SessionBoardProbe`, Ctrl+F6, 30 s of samples to a CSV) rather than a DLL reading raw offsets: reflection and
  component calls do not depend on the game build and nothing of the installed mod changes. Lead to confirm: the
  board is its own actor and its `Flipper` component is the visible deck (seen in the SessionOpenMP and
  ExpandedTricks sources on the user's PC). Result: confirmed in game. Dead ends: no skater property
  leads to the board (`GetSkateboard`, `_skateboard`, ...), `board._flipper` and the truck properties are not readable
  from Lua, the actor has no owner, `_replicatedSkateboardMeshRotator` stays 0 for the local player.
- **Real board recording, step 2, v0.25 (11-27)**: the owner chose one `FindAllOf` after the bone table is sent (up to
  5 tries, nearest `Skateboard` within 2 m, then cached) over reading `skater+0x568` in the DLL (build-dependent,
  unverified). The Lua sends world values every frame (`ASN_SetBoard`, 17 numbers) just before `ASN_SetState`; the
  conversion to mesh space is in `src/board.cpp`, tested with the probe's own values; a scaled mesh or a deck farther
  than 2 m is refused. Figure files get an optional `board` array (7 numbers per frame), written only when **every**
  frame had a board; figure version, clip format and network wire unchanged. Editor: `EditorDoc::OriginBoard` /
  `BoardAt` follow the pose's time rules; the nose mark is turned to the animation's front when an earlier shove-it
  left the physical board turned round; trucks and wheels rigid, 1.7 cm calibration from the riding rows; new setting
  `ui.realboard` (on), the old approximate board stays for recordings without one. The board is never written in game.
  **User test the same day: OK** (KickFlip and Treflip recorded again with their board; log checked).

- **Trick + landing timeline, length and steadiness (11-27, user report with screenshots)**: the 2 s landing felt too
  long; the user gave no preference between the options, so the recommended one: trick + landing <= 2 s
  (`TrickLandingLength`, floor `kLandHoldMin` 0.8 s; editor only: `kGroundTail` and the recordings are unchanged, saved landing clips keep their
  length; an unlocked new landing is therefore held shorter in game too). The jump at the violet line came from panels
  reading `var_` / `doc_`, which are swapped there: what names the edited move now reads the trick (`ShownVar`,
  `ShownFile`), the rows are `EditorDoc::MergeKeyGroups` of both parts, the other part's keys are drawn like open ones,
  and notices that pushed the layout were moved or muted during playback.

- **Late tricks diagnosis and trick list (11-27)**: the game sends `Late_Front_KickFlip`, `Late_Front_HeelFlip`,
  `Late_BackFoot_KickFlip`, `Late_BackFoot_Heelflip`; the catalogue had asset names from SessionTweaks' list
  (`Late_LF_OllieNorth`...). On the user's request the editor's "Seen in game" collection was removed; the catalogue
  is now the whole list, completed with every real name found in the logs and recordings. Recording of late tricks
  (second pop in mid-air, air part about 0.5 s) is diagnosed, not fixed: see `PROJECT_STATUS.md` §4.

- **Shoe soles (11-27)**: calibrating a constant of the mesh on "whatever recording exists" broke when the user
  re-recorded "on foot" while bent over. The toe's reference offset in `_skeleton.json` (left (-10.05, 13.94, 0),
  right opposite) gives the sole axes directly; it agrees with the older upright "on foot" recording.

- **Late flips recorded on their own, v0.26 (11-27)**: chosen by the user over merging ollie and late flip into one
  move (which would change clip selection). A late pop is recognised in the recorder alone (new pop while a trick was
  on at the previous frame), so no name list is needed. Built and tested offline; not installed yet.

- **v0.26 checked in game; names per stance (11-27)**: a trick's on-screen name cannot be derived from its internal
  name ("Switch Late Flip"), so the catalogue got an optional name per stance. Editor option "chest follows the hand"
  now starts off.

- **Trick names from the game, v0.27 (11-27)**: typing one name per trick and stance does not scale. The Lua reads the
  FlipTrick's `DisplayName` at the pop; the names go to a small file of their own (not the figures, which the user
  deletes freely). Learned only for tricks done: reading every definition at load would need a new `FindAllOf`.

- **Switch / fakie late flips have the other foot's name (11-27)**: found from the names read by v0.27 after a user
  report. The studio now builds each card's columns with the move the game shows (`WithStance`); internal names, files
  and the clip format keep the game's own spelling. A name typed by hand from one report had landed on the wrong
  variant: names now come only from the game.

- **Legs after the catch, v0.28 (11-27)**: the leg release at the catch (needed because the game's catch places the
  feet) blocked late flips, where the catch comes at once. The user chose an opt-in per clip over changing the rule by
  trick name, so that existing clips keep playing the same.

- **First release package, v0.28 (11-27)**: GPL-3.0 for the whole project, release number = mod number, nothing
  recorded or authored shipped (every move is recorded by the player). The user asked for an `INSTALLER.cmd` that
  installs UE4SS only when missing, and one that asks where the editor goes. UE4SS is therefore bundled: the loader
  files of the official 3.0.1 zip (same bytes as the tested install), with the four settings values of that install
  and none of its sample mods. An existing UE4SS is never modified; an unconfirmed version stops the installer by
  default, because a wrong one can crash the game (`ue4ss_abi.h`). The source zip comes from the working copy as Git
  lists it, minus the internal working documents, plus the compiled third-party files. Editor icon drawn by
  `tools\make-icon.ps1` (own shapes, no font or image), embedded by `SessionStudio.rc` with the version details.
  One comment of `ue4ss_abi.h` lost a personal local path inherited from upstream.

- **Uninstaller (11-27, user request)**: "put the game back as it was" is done from a record written by the installer,
  not by deleting known folders: clips, other mods and a UE4SS the player already had live in the same places. The first
  run's "was it there before" is never overwritten by an update. UE4SS goes only if we installed it, unchanged, with no
  other mod in `Mods`. Clips are moved, not deleted (agent's choice). `UNINSTALL.cmd` hands over to a temporary copy of
  itself, because it may delete its own folder (cmd failed with exit 1 otherwise, caught by the tests).

- **Clean install and uninstall checked in game (11-27)**: the packaged UE4SS (loader only, four settings changed, no
  `shared` folder, no sample mod) is enough for the mod; the uninstaller left no new item in the game folder.

- **The seam of the two-part timeline is a link, not a copy (11-27, user request)**: the landing's start was copied
  once, when the landing was made. It is now rewritten from the pose seen at the trick's end, in the model, outside the
  undo history (so the trick's undo carries it back). Chosen over a "paste the visible pose" feature, which the user
  did not need. Editor only: clip format and mod unchanged. No switch: an unlocked landing always starts at the
  trick's end; a pose meant for the impact goes a few frames later.

## Problems met and solutions

| Problem | Cause found | Solution |
|---|---|---|
| "Record again" left the old landing on the trick's timeline (user) | the folder rescan reloaded only the open part of the two-part timeline | both parts are reloaded; the game's own landing is rebuilt. Also a Record again button next to Save |
| Installer put the editor into the source folder (user's test) | any folder named `SessionStudio` was used as it was | only an editor folder or an empty one (backups / settings allowed) is used; others are refused |
| Toast over the Welcome card (user's test) | the toast uses the foreground draw list | not drawn while the card is open |
| First package zip unusable (every entry named after the folder) | PowerShell binds the array comma before `+` | parentheses; `make-release.ps1` now reads each zip back and compares the names |
| Axis switch in P2 jumped | no fade on parameter change | every clip / parameter change is faded (P3 rule) |
| Right hand flipped with the same value as the left | local frame turned 180° | go through the mirror; documented fact |
| Capture alone created nothing visible | manual vs nose manual arm poses differ only 5-17° | the editor's job is to retouch poses |
| Stretched / twisted shin in game, fine in editor (11-24) | first blamed on other mods; a diagnostic DLL showed the **game's own trick animation** stretches the shin (up to +15 cm) | leg chains written by clips use reference bone lengths + post pass after the other mods |
| Editing mid-clip changed the start keys | pelvis-only clip seen as empty → figure defaults (hold on) | defaults only when no tracks and no pelvis keys |
| Lone pelvis key moved the whole clip; skeleton "in the air" | a single key applies everywhere; pelvis beyond leg reach | auto start key at the game pose; `PelvisReachScale` |
| Wrong clip while running | speed-based "mounting" fired on foot | new Running state; Riding only when on the board |
| Pose jump when stepping off the board | riding offset left in 0.25 s (7°/frame) | distance-based fades; dismount part |
| Crushed shoulder on "scratch the head" clip | game mesh has no twist bones | shoulder check, "Fix the shoulder", auto clavicle |
| Undo did nothing with Auto key | automatic commit pushed its own undo step | quiet commit adds no step |
| Thigh spinning on the rings in clips with pelvis keys | rings measured the pose after the pelvis leg re-solve | rings and IK use the pose without pelvis offset |
| Trick landing keys fell mid-air or late | clips played on the clock from the pop, jumps vary | landing sync (`play.markers`, `TrickClock`, predicted ground) |
| Prediction ~2× too long for flip tricks | `IsLanding` comes later on flips | learned lead and correction per trick and stance (v0.17) |
| Peer jumps in multiplayer (v0.19) | stateless extrapolation reset the time each packet | receiver clock pulled softly toward sent values (v0.20), step limits (v0.22) |
| Two players with their own kickflip | clips matched by file name | content id + `clips\others\` (v0.21) |
| Landing clip cut before 1 s | two fixed 0.8 s limits | landing lasts as long as its clip, up to 2 s (v0.23) |
| "Open the guide" launched another program | user's `.md` file association | HTML guides generated by `md2html.js`, Notepad fallback |
| Named grinds | `_replicatedGrindName` empty offline | parked; generic grind support kept, hidden in the editor |
| Live preview in game (probe C0) | camera locked on the board, game pauses without focus, board did not move | abandoned; mannequin drawn by the editor instead |
| Editor board did not flip | skeleton board bones ≠ visible physics board | approximate board kept as an option, off by default |
| Agent's screenshot sent a key to the user's window | automation while the user used the PC | rule: no keystrokes or captures while the user may be at the PC |

## 2026-11-27 — one landing per trick clip (mod v0.29)

- User: a deleted clip left its landing behind; each trick clip must have its own landing. Cause: files matched by trick
  name only (editor and player). Chosen by the user over an editor-only change: the link also holds in game.
- Optional root field `link` (same random id in both files), rather than a file naming rule: it survives renames, and
  older versions keep unknown fields. Written only by Save / Duplicate, so untouched files are unchanged.
- Player: remembers the link of the trick clip that played since the last pop, as a hash in one word (read from the Lua
  side too). Clips for "every trick" never change it. Network format unchanged.
- Older shared landing: given to one trick clip (the one that is on), never deleted with another one.
- The "Landing of this trick" button is gone: it could open another clip's landing.
- Offline tests and build only; installed for the user's game test (see `PROJECT_STATUS.md` §0).

- Same day, user's test: two kickflip clips each played their own landing; deleting one removed its landing, with the
  confirmation naming it. The user then asked for commit, push and the v0.29 release.
- GitHub release `v0.29` published by the owner (first published without its files, added right after); v0.28 left
  as it is. Attached files checked against the local ones (SHA-256).

## 2026-11-28 — optional skinned body model in the editor

- The owner received a character model of the game from its developers and says they allow redistributing it. Asked to
  show it instead of the generated mannequin.
- Loaded at run time from `body.glb` next to the editor; the generated mannequin stays as the fallback. No model data
  in the code or tests; the file is not in Git (a public history cannot be taken back) and not yet in the package.
- Own small .glb reader on the existing JSON parser (no new library). Bind pose taken from the inverse bind matrices,
  axes converted by swapping y and z, x100. Joints matched to the game's bones by name. CPU skinning, same renderer.
- One clay colour as before; bone tints are mixed by vertex weight; the stripe colours the surface facing the limb's +Y.
  Textures left for later. The model has a face, which the owner had not wanted on the generated mannequin.
- Offline tests, offline check on the real file and self-test only: not yet seen in the editor.
- Same day: the owner saw the clay version in the editor, liked it and asked for colours. The file has colour images
  for the skin, face and eyes only, none for the clothes: flat colours per part, chosen from words in the material
  names; the cues became tints over them. Real textures still left for later.
- Same day: the owner approved the result and asked to remove the red shoulder alert for good. Removed on both bodies,
  in the skeleton view and in the inspector (the figures stay, in the normal text colour), guides updated. The
  measurement (`ShoulderInfo`), the fix button and the automatic natural shoulder are unchanged. Nothing warns any more
  of an arm likely to crush the shoulder in game.

- Same day, v0.30: the owner asked to push and to prepare the release. Asked about the model, they said to include it,
  not to credit anyone, and that they have formal authorization. The package ships the file unchanged as
  `files\Editor\body.glb` (the installer already copies the whole editor folder and records each file, so uninstall
  covers it); `make-release.ps1` got `-Body` / `-NoBody` and refuses a `.glb` in the source zip. A neutral notice (no
  name) says the model is not under the GPL. The mod only changes its version number.

- GitHub release `v0.30` published by the owner (latest; tag on `420779e`; v0.29 left as it is). The three attached
  files checked against the local ones (SHA-256). Packaging checks: no `.glb` in the source zip, packaged model identical
  to the original, install and uninstall on a made-up game folder. v0.30 of the mod was not tried in game before the
  release as far as the agent knows. To fix later: the `--selftest` line about `body.glb` without a skeleton.

- Same day, v0.31: random draw between the clips of a trick. The owner asked for few new buttons: the existing clip
  switches define the draw; the only new control is a **Game animation** row among the clips (owner's choice over
  "always in the draw"). Choices made by the agent and told to the owner: never the same result twice in a row (so two
  choices alternate), top priority only, an "every trick" clip is never drawn, when the game's animation comes out no
  landing of that trick plays. The draw is made once per pop in the player (never in `Choose`) and remembered by file
  name; the setting is a new file, `_random.json`, so clips and `_studio.json` are untouched.

- v0.31 installed on the owner's game and tried the same day (normal stance, regular): "it works". The log of the run
  shows 32 draws without an immediate repeat. Multiplayer, goofy and other stances not tried.

- v0.31 pushed (`6b3cbea`) and packaged locally for the owner to publish. Checked: packaged DLL, Lua, editor and model
  identical to the tried ones; no clip or recording in the player zip; no `.glb` or binary in the source zip. The
  installer test on a made-up folder was **not** run: the owner's game was running and the installer refuses then; the
  install and uninstall scripts have not changed since v0.30, where that test passed.

- GitHub release `v0.31` published by the owner (latest; tag on `6b3cbea`). The three files checked against the local
  ones (SHA-256). The install and uninstall test on a made-up game folder, skipped at packaging time, then passed.

## Gaps in this history

- No Git history before 2026-11-26: intermediate source states exist only as local zip backups in `_archive/backups/`.
- Some features were built with "user test pending" and no later verdict was written down (see `PROJECT_STATUS.md`,
  level CODE).
- Test counts quoted in old notes are not comparable with today's (real-file tests depend on the machine).
