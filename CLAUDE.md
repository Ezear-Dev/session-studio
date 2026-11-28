# CLAUDE.md — permanent instructions for Session Studio / Session Anim Loader

Read this first, then `PROJECT_STATUS.md` (what works now) and, when you need the why, `DEVELOPMENT_LOG.md`.
Local-only working notes live in `notes/` (not in Git): `notes/HANDOFF.md` holds the detailed install history and
rollback paths. If documents and code disagree, **trust the code and tell the user**.

## 1. What the project is

Animation tools for the game **Session: Skate Sim** (Unreal Engine 4.27, Windows, Steam). Unofficial fan project,
public GitHub repository, GPL-3.0, first release `v0.28` published 2026-11-27, then `v0.29` the same day; `v0.30` published 2026-11-28; `v0.31` (random trick draw) built the same day (check the Releases page). Never change the repository's visibility unasked.

| Part | Folder | In the game's `Win64\Mods` | Role |
|---|---|---|---|
| Session Anim Loader, native | `SessionAnimLoader/Native` | `SessionAnimLoaderNative\dlls\main.dll` | C++ UE4SS 3.0.1 mod: plays `.sanim` clips on top of the game's finished pose |
| Session Anim Loader, Lua | `SessionAnimLoader/Lua` | `SessionAnimLoader\` | reads the game state every frame, passes it to the DLL, keys, multiplayer |
| Session Studio | `SessionStudio` | none (separate program) | external editor (Dear ImGui 1.93.0 + ImGuizmo, Direct3D 11) that writes `.sanim` clips |

Keep the mod and the editor separable: a player can use the mod without ever opening the editor.
Before 2026-11-26 everything was called **AnimStudio**; that name survives only in old notes, backups and the
`asn_` / `ASN_` prefixes in the code (keep the prefixes).

## 2. Architecture

```
game state ──► Lua (per-frame hook) ──ASN_* functions──► DLL: Player chooses clips, blends layers
                                                          │  writes the pose buffer in FinalizeBoneTransform
editor ──writes .sanim──► clips folder ◄──rescan 1 s, reload on content change── DLL
DLL ──records──► clips\_figures\*.json, clips\_skeleton.json ──read by──► editor
DLL ──writes──► SessionAnimLoader.native.log ──parsed by──► editor status strip / setup checker
```

- `SessionAnimLoader/Native/src/` = **pure logic, no UE4SS** (static lib `asn_core`): clip format and evaluation
  (`clip`), clip set and selection (`clipset`, `player`), states and tricks (`trick`), mirror, pelvis offset, step
  cycle (`cycle`), landing sync (`trick_sync`), landing hold, figure recording (`figure`, `capture`), network wire
  (`net`), random trick draw (`random_draw`), JSON, math, skeleton.
- `SessionAnimLoader/Native/main.cpp` = the only file that touches the game: vtable hook, chaining behind
  ExpandedTricks, `ASN_*` Lua functions, `kVersion`.
- `SessionStudio/model/` = **editor logic without interface** (static lib `asn_editor`, links `asn_core`): `EditorDoc`
  (split over `editor_doc_*.cpp` by feature), IK, mannequin and board meshes, the optional skinned body (`body_model`: `body.glb` next to the editor, fallback to the mannequin; the model file is never in Git), time view, view math, Browse cards and the "which clip plays" rule (`library`), workspace
  (settings, backups, library file operations, game log parsing), paths.
- `SessionStudio/app/` = interface only: one `App` class (`app.h`) split by panel since 2026-11-26 —
  `app.cpp` (core: files, clips, confirmations, frame), `app_shell.cpp` (workspaces layout, top bar, game pill, toasts, welcome),
  `app_browse.cpp` (Browse page), `app_library.cpp` (the Animate drawer), `app_stage.cpp` (3D view, floating tools), `app_inspector.cpp` (Inspector card in the stage), `app_landing.cpp` (a trick and its landing clip on one timeline), `app_timeline.cpp`, `app_dialogs.cpp` (dialogs with one look: `BeginDialog` / `DialogButtons`, Preferences, the shortcuts list,
  shortcuts); shared helpers in `app_internal.h` (namespace `app_detail`); `ui_theme.*` (palette, style, fonts, icons:
  **every colour comes from here**) and `ui_widgets.*` (themed buttons, toggles, switch, chips); `stage_renderer.cpp` (D3D11 mannequin render), `main.cpp` (window, loop, `--selftest`).
- The editor reuses the mod's core library: **any change in `src/` affects both the DLL and the editor**.

Rule for new code: logic goes in `src/` or `model/` with offline tests; `app/` only draws and dispatches.
Stage rule (redesign step 3): anything clickable over the 3D view must be a child window of the stage window (see
`StageOverlays`), never a plain item: ImGuizmo and the stage picking ignore the mouse only over other windows.

## 3. Build and test (the only commands known to work)

Tools: Visual Studio 2022 Community on `D:` (MSVC 14.44, bundled CMake), Windows SDK. They are **not on the PATH**;
the scripts call `D:\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`.

| Command | Result |
|---|---|
| `SessionAnimLoader\Native\test.cmd` | builds `asn_tests` and runs every offline test; last line `ALL PASS (N checks)` |
| `SessionAnimLoader\Native\build.cmd` | builds DLL + editor into `%LOCALAPPDATA%\SessionStudio\build`, copies `main.dll` to `SessionAnimLoader\Native\dist\` and `SessionStudio.exe` to `SessionStudio\dist\` |
| `node SessionStudio/docs/md2html.js SessionStudio/docs` | regenerates the guide `.html` pages after a guide edit |

- Run the `.cmd` files from PowerShell with `& "<full path>\test.cmd"` (from Git Bash, `cmd //c test.cmd` failed).
  A harmless `'vswhere.exe' is not recognized` line appears.
- `asn_tests` tools: `--check-clips <dir>`, `--write-test-data <dir>`, `--shoulder <dir> <clip> [--fix]`,
  `--replay-states <clips> <log> <from> <to>`, `--convert-landing <dir> [--apply]`. Editor: `SessionStudio.exe --selftest`.
- Some tests read the real clips in the game's folder (default Steam path) and are skipped without them, so the
  check count varies between machines.
- `third_party/imgui` (commit c1aaac6) and `third_party/ImGuizmo` (commit 105e4fd) are not in Git (see
  `DEVELOPMENT.md`); without them only the editor executable is skipped. `UE4SS.lib` is generated from `UE4SS.def`.
- `build.cmd` overwrites `dist\`. If the user's editor is open, the copy fails: stage the new exe under another name
  and tell the user. Back up the previous `dist` binaries before replacing them.
- Commands run by Claude see a redirected `%LOCALAPPDATA%` (MSIX). Read files the game writes through
  `\\localhost\C$\Users\<user>\AppData\Local\...`.

## 4. Development rules

**Working with the user**
- Chat in **French**; everything written in the project (code, comments, UI, logs, docs, commits) is in **English**.
- The user is an amateur modder, not a developer, not a skater, and a Git beginner: explain in plain words, define
  terms, correct them when they are wrong (say why and how sure you are); the decision stays theirs.
- No design documents or approval ceremonies: a short plan in chat, then code. Ask short questions with concrete
  options only when the answer changes what you build.
- One feature or fix at a time, small enough to test. Commit or push **only when asked**.
- Report honestly: separate **verified** (how), **not verified**, and **guess**. Never claim a test passed without
  its output. Never invent facts about the game, the engine or other mods.

**Game and install**
- **Never launch the game.** The user tests in game; you give exact steps, then read the logs and ask what they saw
  (logs do not show the rendering).
- Install only while the game and the editor are closed; back up what is installed first; install DLL, Lua and
  editor from matching versions. Never modify the original Steam files.
- Never send keystrokes or take screen captures while the user may be using the PC.

**Code constraints**
- Lua: everything runs in the per-frame hook `SkaterCharacterBase:GetSkateboardingAnimParams` (game thread).
  No `LoopAsync`, no `FindAllOf` while playing (one exception, the board search after the bone table, §5); key
  handlers only raise a flag. No Lua interpreter is available
  offline: read Lua changes twice.
- DLL: touch only the mesh given by the Lua, check the bone count every frame, keep `__try` around game memory,
  refuse cleanly (log, no crash) when offsets do not match the game build.
- **Compatibility:** existing `.sanim` clips must keep loading and playing the same. Ask the user before changing the
  clip format, the meaning of a key, or the multiplayer wire format. Unknown top-level clip fields are preserved.
- Bump `kVersion` (`main.cpp`), `VERSION` (`main.lua`) and the version in `SessionStudio/app/res/SessionStudio.rc`
  together; the mod must match the editor that reads its log lines. `tools/make-release.ps1` refuses a mismatch. The
  release number is the mod's (owner's choice, 2026-11-27).
- Releases (`DEVELOPMENT.md`, "Release package"): only `tools/make-release.ps1` builds the player package (explicit
  file list, into `release/`, not in Git), with the matching source zip. No clip and no recording is ever shipped. The player package (never the source zip or Git) holds
  the editor's character model as `body.glb` (owner, 2026-11-28; see the legal rules below).
  The installer (`installer/`) never modifies an existing UE4SS and never replaces anything in `clips`. It writes down
  all it does (`Win64\SessionAnimLoader_install.json`); `uninstall.ps1` undoes only that, back to the state before the
  FIRST install. Without a record it touches nothing; it never deletes clips and never removes a folder that is not
  empty. A new installed file must go through `Install-File`. The editor always gets its own sub-folder.
  Only UE4SS **3.0.1** (the experimental build once crashed the game with a C++ mod: see `ue4ss_abi.h`).
- The editor parses DLL log lines (`model/workspace.cpp`): changing a log line means updating the parser and its test.
- Every new user-facing string is English; there is no translation layer any more.

**UI principles** (owner's rules): simple by default, depth on demand (Creator / Studio mode, no
duplicated features); hard to break (undo for every edit, backup before overwrite, safe defaults); readable at a
glance (warnings say what is wrong and offer a fix); **honest preview** (the editor is not the game: never imply
they match unless checked in game). For each new feature, say what the casual player sees and what the advanced
creator gains.

**Repository hygiene and legal**
- Working notes, plans, test data, logs, packages, `ManualTurnRate/` and `_archive/` stay out of Git (see
  `.gitignore`). Obsolete material goes to `_archive/`. No personal paths or private data in the repository or packages.
- Skate Style and other closed tools: ideas only, no code, no assets. ReSkate Animator Studio is GPL-3.0 (per our
  study): ask before reusing any of its code. No game asset in the repository.
  **One exception, in the release package only:** the character model `body.glb`. The owner states they have the
  developers' formal authorization to distribute it (2026-11-28; no written text is in the project) and chose not to
  credit anyone. It is not under the GPL and the notices say so. This covers that file only: ask before shipping any
  other game asset, and never add a model to Git.
- **Licence: GPL-3.0 for the whole project** (owner's choice, 2026-11-27; copyright "Ezear-Dev"). A distributed binary
  needs its source beside it (the source zip, attached to the same release). Third-party notices:
  `THIRD-PARTY-NOTICES.txt` (update it when a dependency changes).

## 5. Technical facts you must not rediscover

- Hook: vtable slot `FinalizeBoneTransform` of `USkaterSkeletalMeshComponent` = `*(mesh) + 0x938`; editable buffer
  `TArray<FTransform>` at `mesh + 0x4b0 + i*0x10`, `i = *(int*)(mesh + 0x4f0)`; UE 4.27 `FTransform` = 48 bytes,
  component space. Build-specific: a game update may make the DLL refuse to install.
- Chain: Session Anim Loader → ExpandedTricksNative → engine (SessionTweaks pre-hook). ExpandedTricks takes the slot
  once and refuses anything but the engine function, so we always install **after** it.
- The game's own trick animation stretches the shin in the air; leg chains written by clips use reference bone
  lengths, and a post pass after the other mods restores them (`legPostPass` in `_studio.json`).
- Skeleton: 70 bones, parents before children; board bones `SKXX_SkateSkel_*` from index 58. The skeleton's board
  bones are **not** the visible physics board (they do not flip).
- The **visible board** (probe of 2026-11-27, regular stance): actor `Skateboard` (class `PBP_SkateboardEx_C`), deck =
  its component named `Flipper` (rotation read with `K2_GetComponentToWorld().Rotation`, which Lua can read directly).
  No skater property leads to it and it has no owner, so it needs `FindAllOf`: allowed **only after the bone table is
  sent** (owner's decision, 2026-11-27), never during play. In the mesh's component space it rides at (0, 0, 7.0),
  unrotated; a kickflip turns it 360° about its X, and after a shove-it it stays turned round. It is recorded into
  the move recordings (`src/board.h`, figure field `board`), never written in game.
- `FootPositionType`: 1 normal, 2 fakie, 3 nollie, 4 switch; fakie / nollie exist only crouched (and for tricks).
- **Trick names** (mod v0.27): at each pop the Lua sends the FlipTrick's `DisplayName`; the DLL keeps them per full
  name in `clips\_trick_names.json`, which the editor reads. Never type a name by hand into the catalogue. The editor's
  trick list is the catalogue (`trick.cpp`) alone, with the names the game really sends (`Late_BackFoot_...`,
  `Late_Front_...`), not asset names (`Late_LF_...`).
- **Late tricks** are a second pop in mid-air (about 0.5 s to the ground; the catch / IsLanding comes almost at once).
  In **switch and fakie** the game names a late flip with the other foot than its internal name (normal and nollie do
  not; goofy never checked): cards follow `WithStance`. A trick clip gives its legs back at the catch unless
  `play.legsAfterCatch` (v0.28).
- The 3D mannequin's shoe soles come from the skeleton's reference toe offset (sole up = foot bone X), never from a
  recorded pose.
- **Random draw** (v0.31, owner's choice): the clips of one trick that are on (same stance and foot, top priority) are
  drawn once per pop in `Player::DrawTrick`, never the same twice in a row; `clips\_random.json` (written by the
  editor) adds the game's own animation per figure. The pool rule lives once, in `src/random_draw.h` (`TrickPool`),
  for the player and the editor. No randomness inside `Player::Choose` (it runs every frame).
- A trick clip and **its own** landing clip share the field `link` (v0.29): in game a linked landing plays only after
  that trick clip. Never pick a landing by trick name alone (`model/library.h`: `LandingForTrick`, `LandingsOfTrick`).
- Replay = `CustomMovementMode == 6`: the mod stands by (the replay already records our layer).
- Mirroring is per bone; the right hand's local frame is turned 180° from the left. The editor mirrors its 3D view on
  Y (Unreal is left-handed). The game mesh has no twist bones (raised arm without clavicle crushes the shoulder).
- Multiplayer: SessionOpenMP mod channel `animloader.clips1`, wire text `S2` + hex (`src/net.h`), 15 frames/s; a clip
  is identified by its content (`ContentId`, FNV-1a 64 without BOM and `\r`), never by its file name.
- Figures (game recordings) are named `<Move>_<RGS|SWS|FKS|NLS>_<regular|goofy>`; a `.refaire` file asks the DLL to
  record a move again.

## 6. Project memory management

These files are the source of truth for project continuity:

- `CLAUDE.md`: permanent project instructions and technical constraints.
- `PROJECT_STATUS.md`: current project state and known issues.
- `DEVELOPMENT_LOG.md`: significant development history and decisions.

At the end of every significant development task:

1. Update `PROJECT_STATUS.md` if the project state changed.
2. Append significant technical decisions and milestones to `DEVELOPMENT_LOG.md`.
3. Update `CLAUDE.md` only when permanent instructions or architecture change.
4. Never claim a feature is working without verification.
5. Never overwrite historical information without justification.
