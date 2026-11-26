# CLAUDE.md — permanent instructions for Session Studio / Session Anim Loader

Read this first, then `PROJECT_STATUS.md` (what works now) and, when you need the why, `DEVELOPMENT_LOG.md`.
Local-only working notes live in `notes/` (not in Git): `notes/HANDOFF.md` holds the detailed install history and
rollback paths. If documents and code disagree, **trust the code and tell the user**.

## 1. What the project is

Animation tools for the game **Session: Skate Sim** (Unreal Engine 4.27, Windows, Steam). Unofficial fan project,
private GitHub repository, to be released open source later. Never change the repository's visibility unasked.

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
  (`net`), JSON, math, skeleton.
- `SessionAnimLoader/Native/main.cpp` = the only file that touches the game: vtable hook, chaining behind
  ExpandedTricks, `ASN_*` Lua functions, `kVersion`.
- `SessionStudio/model/` = **editor logic without interface** (static lib `asn_editor`, links `asn_core`): `EditorDoc`
  (split over `editor_doc_*.cpp` by feature), IK, mannequin and board meshes, time view, view math, workspace
  (settings, backups, library file operations, game log parsing), paths.
- `SessionStudio/app/` = interface only: `app.cpp` (one large `App` class, ~3100 lines: header, library, stage,
  inspector, timeline, dialogs), `stage_renderer.cpp` (D3D11 mannequin render), `main.cpp` (window, loop, `--selftest`).
- The editor reuses the mod's core library: **any change in `src/` affects both the DLL and the editor**.

Rule for new code: logic goes in `src/` or `model/` with offline tests; `app/` only draws and dispatches.

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
  No `LoopAsync`, no `FindAllOf` while playing; key handlers only raise a flag. No Lua interpreter is available
  offline: read Lua changes twice.
- DLL: touch only the mesh given by the Lua, check the bone count every frame, keep `__try` around game memory,
  refuse cleanly (log, no crash) when offsets do not match the game build.
- **Compatibility:** existing `.sanim` clips must keep loading and playing the same. Ask the user before changing the
  clip format, the meaning of a key, or the multiplayer wire format. Unknown top-level clip fields are preserved.
- Bump `kVersion` (`main.cpp`) and `VERSION` (`main.lua`) together when the mod changes; they must match the editor
  that reads their log lines.
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
- The native mod is GPL-3.0 (SessionOpenMP ABI header, ExpandedTricks hook method). No licence chosen for the rest;
  a distributed editor will very likely have to be GPL-3.0: raise it before any release (`notes/RELEASE-CHECKLIST.md`).

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
- `FootPositionType`: 1 normal, 2 fakie, 3 nollie, 4 switch; fakie / nollie exist only crouched (and for tricks).
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
