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

## Problems met and solutions

| Problem | Cause found | Solution |
|---|---|---|
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

## Gaps in this history

- No Git history before 2026-11-26: intermediate source states exist only as local zip backups in `_archive/backups/`.
- Some features were built with "user test pending" and no later verdict was written down (see `PROJECT_STATUS.md`,
  level CODE).
- Test counts quoted in old notes are not comparable with today's (real-file tests depend on the machine).
