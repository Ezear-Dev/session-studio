# PROJECT_STATUS.md — real state of the software

Snapshot of **2026-11-26** (Git `7610976`, mod v0.24, clip format `sanim` version 1, game: Steam build of November
2026, UE4SS 3.0.1). Update this file after each meaningful change.

**Evidence levels used below**
- **GAME ✔** — checked in game by the user (date and source given).
- **OFFLINE ✔** — covered by `asn_tests` (logic only; says nothing about what the player sees).
- **CODE** — present in the code, no user test result found in the notes.
- **✘** — tried and did not work, or parked.

## 1. Build and test state

| Item | State | Evidence |
|---|---|---|
| Offline tests | **ALL PASS (18607 checks)** | `test.cmd` run on 2026-11-26 for this document (exit code 0) |
| Clips in the game folder | 27 loaded, 0 refused | `asn_tests --check-clips <game clips folder>`, same run |
| DLL + editor build | `dist\main.dll` and `dist\SessionStudio.exe` dated 2026-11-26 20:43-20:44 (rename build) | file dates; `build.cmd` **not re-run** for this document |
| Installed on the user's game | v0.24 (DLL + Lua + 122 clip files), 2026-11-26 20:47 | `notes/HANDOFF.md`: user test OK, hooked behind ExpandedTricks, editor saves reloaded, Ctrl+F3 works |
| Editor self-test | new folders found, DLL and Lua present | `HANDOFF.md`, 2026-11-26 |
| Multiplayer on v0.24 | **not tested** | — |

The check count depends on the machine: tests on real game clips are skipped when the game folder is missing
(earlier notes quote 10842 or 4924 checks for older states).

## 2. Session Anim Loader (game mod)

| Feature | State | Evidence / notes |
|---|---|---|
| Pose hook, chained behind ExpandedTricks, with and without ET | GAME ✔ | P2 2026-11-22, map change covered 2026-11-23, v0.24 install 2026-11-26 |
| Clip player: additive / replace, fades, cross-fades, hot reload, broken file keeps last good version | GAME ✔ | P3 (7 in-game tests, 2026-11-23); later lots |
| Goofy mirror per bone (`mirrorForGoofy`, `_studio.json` overrides) | GAME ✔ | P3 test 5 |
| Capture of the game pose (Ctrl+F4), mod on/off (Ctrl+F3) | GAME ✔ | P3; Ctrl+F3 retested on v0.24 |
| Manual / nose manual clips driven by `ManualRatio` | GAME ✔ | P3 / P4 |
| Flip tricks per trick, stance and feet | GAME ✔ | P5b / P6, 2026-11-23/24 |
| Leg chains keep reference lengths + post pass after other mods | GAME ✔ | "c'est clean", 2026-11-24 |
| Automatic recording of the game's moves (figures) + "record again" | GAME ✔ | P4, P6; variants turn green in the editor |
| Base states OnFoot, Riding, Pushing, Crouch (incl. fakie / nollie), Landing | GAME ✔ (detection) | logs of 2026-11-25; README lists them as working |
| Landing sync (trick clip follows jump height, landing = own clip) | GAME ✔ | v0.16, "C'était bien calé" 2026-11-26 |
| Learned landing correction per trick (v0.17) | CODE | installed after the v0.16 test; never judged on its own |
| Landing state up to 2 s (length of the landing clip) | GAME ✔ | 2026-11-26 19:06-19:12, log + user |
| Dismount part of the riding clip, distance-based fades | CODE + OFFLINE ✔ | v0.14; README lists dismount as working, no dated user verdict found |
| Pelvis offset with feet kept planted, pelvis reach limit | CODE + OFFLINE ✔ | lot 2 / v0.10; the start-key + reach fix was accepted by the user |
| Held pose (`play.hold`) | CODE + OFFLINE ✔ | option; user found it lifeless, off by default |
| Replay stand-by | partly GAME ✔ | user saw trick clips in the replay (so stand-by is the right rule) |
| Walking / Running, step-cycle drive (`drive: cycle`) | ✘ parked | bugs remain (user, 2026-11-25); cycle sync never tested in game |
| Grinds | ✘ partial | generic Grind state exists; named grinds fail (`_replicatedGrindName` empty offline); hidden in the editor |
| Grabs | ✘ | state exists (`GrabState`), never verified, hidden |
| Powerslide | CODE | never checked in game |
| Multiplayer (SessionOpenMP): clip layers sent, peers play their own copy | GAME ✔ on v0.21 | 2-player test 2026-11-26: both saw each other, 0 snaps; small stutters on the peer at low jumps |
| Smoother peer clock (v0.22) | OFFLINE ✔ | simulation only; no 2-player test since |
| `clips\others\` for other players' clips | GAME ✔ on v0.21 | same test |

## 3. Session Studio (editor)

| Feature | State | Evidence / notes |
|---|---|---|
| Open figure, key bones with rotation rings, play, undo, save, game reload | GAME ✔ | P4 (10 tests, 2026-11-23) |
| Two-bone IK for hands (P5a) | GAME ✔ | P5a validated 2026-11-23 |
| Hold keys, IK handles on elbow / shoulder / knee (P5c) | CODE + OFFLINE ✔ | no recorded verdict |
| Limb bar / body-part tools, chest follows the hand, pelvis tool | CODE + OFFLINE ✔ | lot 2 |
| Smooth keys as a C1 spline, interpolation smooth / linear / step | OFFLINE ✔ | 2026-11-24 / 25 |
| Shoulder check and "Fix the shoulder" | OFFLINE ✔ | applied to the user's clip with `--shoulder --fix`; in-game look not recorded |
| UI phase 1 (resizable panels, header, status strip, backups on save) | CODE + OFFLINE ✔ | user test pending per notes |
| UI phase 2 (Library: Tricks / Clips / Captures, rename, duplicate, delete) | CODE + OFFLINE ✔ | menus never checked with a real mouse; "partly untested" (`HANDOFF.md`) |
| UI phase 3 (Stage toolbar, Inspector, Creator / Studio mode) | GAME ✔ (editor) | user: "tout est ok", 2026-11-25 |
| UI phase 4 (timeline zoom, pan, ruler, transport) | GAME ✔ (editor) | user: all tests validated, 2026-11-25 |
| UI phase 5 (welcome window, setup checker, guides) | CODE | agent screenshots only |
| Editor loop / speed (x1, x0.5, x0.25) | CODE | — |
| Undo with Auto key fix; leg rings fix with pelvis keys | CODE + OFFLINE ✔ | user check pending (`HANDOFF.md`) |
| Copy / paste of pose and keys, end-pose warnings | GAME ✔ (editor) | user check OK 2026-11-26 |
| Lock start and end (`lockEnds`) | GAME ✔ | user check OK in editor and game, 2026-11-26 |
| Landing-of-this-trick workflow (open / create landing clip) | GAME ✔ | part of the landing sync validation |
| Solid mannequin view (body / skeleton), fingers, head direction marker | GAME ✔ (editor) | "Looks good", 2026-11-26 (sixth look) |
| Approximate board (option, off by default) | CODE | known not to match the real board |
| `--selftest` | ✔ | output in `dist\selftest.txt` |

## 4. Known problems and points to watch

- **Multiplayer versions:** v0.24 changed the channel to `animloader.clips1`; a v0.21-v0.23 player and a v0.24
  player cannot see each other. The friend still has v0.21. `_archive/tester-packages/` describes old versions
  (v0.18-v0.21); a new package must be written from scratch.
- **Peer stutters:** small stutters remained on the peer's screen in v0.21; the v0.22 fix is only simulated.
- **Game updates** can move the hook offsets: the DLL then refuses to install (by design, no crash).
- **Unexplained crash** on 2026-11-23, ~2 min after a map change (`ACCESS_VIOLATION`,
  `SessionGame-Win64-Shipping.exe+0x3233233`, no mod on the stack). Not reproduced.
- **Landing log artefact:** lines "landing: lasted 6-8 s" when the game is paused or in replay (cosmetic, not fixed).
- **Editor board ≠ game board:** the skeleton's board bones do not flip; the real board is a physics actor.
  Recording it would need a DLL + Lua change and an in-game probe first.
- **Editor preview ≠ game:** physics, SessionTweaks' `body_feel` and game blending apply on top. The live-preview
  idea (probe C0) was a **no-go** on 2026-11-26 (camera locked on the board, game pauses without focus).
- **Agent limits:** simulated mouse clicks do not reach the ImGui window; mouse-driven UI is verified only by the user.
- **Editor often left open** by the user: new builds cannot overwrite `dist\SessionStudio.exe` while it runs.
- **Out-of-date docs** (identified for this document, not fixed): `SessionStudio/docs/Reference.md` still says the
  editor "shows bones, not the skinned body" and that multiplayer is "v0.19, 2-player test pending";
  `notes/HANDOFF.md` mentions a `tester-package/` folder that no longer exists at the root (it is in
  `_archive/tester-packages/`); `notes/RELEASE-CHECKLIST.md` still uses AnimStudio names.
- **Licence** of the editor and Lua parts not chosen; to settle before any release.
- Leftover diagnostic: "diag legs" lines are still logged per trick (`main.cpp`; cheap; "remove later" per the
  2026-11-24 notes).

## 5. Ideas recorded, not started

- Emotes (`notes/EMOTES-design.md`, 4 decisions pending with the user).
- Phase sync for pushing; recording the real board; packs (export / import); problem-report zip.
- ReSkate-inspired items not done yet (`notes/STUDY-RESKATE-EDITOR.md` §12.2): non-overshooting smooth keys (changes
  how clips look, DLL too), snap foot to deck, multi-part selection, solo rotation, joint limits, per-key blends,
  curve view, first-run tour.
- Mannequin stage 3 (click on the body, hover, ghost body): ask the user first.
- **Set aside by the user — do not propose again unasked:** clips chosen by jump height.

---

## 6. UX/UI redesign project (in preparation, nothing started)

### 6.1 Goal (user, 2026-11-26)
Turn Session Studio into a small, professional, modern and intuitive animation studio, in the spirit of 2025-2026
creative software. **Every existing feature is kept.** Choosing tricks, stances and feet must become much more
intuitive. The **3D viewport and the timeline** become the centre of a premium creation experience.

### 6.2 Fixed constraints
- No change to the `.sanim` format, the DLL, the Lua or the network format **unless the user agrees first**; the
  redesign is expected to be editor-only (`SessionStudio/app`, plus `model/` where logic is needed).
- Keep the `model/` / `app/` boundary: new behaviour goes into `model/` with offline tests.
- Honest preview: no visual promise that the editor shows exactly what the game will do.
- Creator / Studio principle: one tool, simple by default, depth on demand, no duplicated features.
- Dear ImGui **1.93.0 without docking** (the bundled branch). Docking, multi-viewport or another UI library would be a
  dependency change: decide it explicitly. The 3D view already renders through Direct3D 11 (`stage_renderer.cpp`).
- `app.cpp` is a single ~3100-line class: splitting it into panels (library, viewport, inspector, timeline, dialogs)
  is likely the first technical step, done without behaviour change and checked by the user.
- The agent cannot drive the mouse in the editor: every interaction change needs a precise user check list.

### 6.3 Feature inventory the redesign must keep (from the code, `app.cpp` panels)
- **Header:** variant pill, clip name, save state badge, Save, Undo / Redo with labels, Auto key, Help.
- **Library:** variant bar (stance × feet, recorded or "do once in game"), Tricks tab (search, categories, every
  trick seen in game), Clips tab (this variant / all; enable, open, rename, duplicate, delete to backups, "changes
  nothing" warning, unreadable files in red), Captures tab, record again.
- **Stage (3D view):** body parts 1-7, tools Rotate / Pull (IK) / Move pelvis, Local / World rings, ghost, body /
  skeleton view, optional board, camera presets (centre F, start, quarter turn, top), shortcut list.
- **Inspector:** Selection (IK options, chest follows hand, elbow / knee swivel, shoulder check and fix, mirror,
  copy the game's pose; Studio: precise values, track weight, bone list), Clip (name, mode, enabled, regular + goofy
  auto mirror, held pose, landing of this trick / back to the trick, dismount part, duration, copy to another variant,
  lock start and end, competing clips), Advanced (stances, feet, all flip tricks, priority, blends, loop, drive,
  ManualRatio range).
- **Timeline:** rows grouped by limb, game row with its own slider, markers (air, catch, landing, ground, hang time,
  dismount), padlocks, key select / box / drag / delete / interpolation menu, copy / paste, zoom, pan, fit, compact
  rows, empty rows, transport, loop, speed, end warnings.
- **Status strip and safety:** game and mod state from the log, last save + reload result, clips folder, backups,
  welcome window with setup checker, guides.

### 6.4 Known UX pain points to address
- Tricks, stances and feet: today the user picks a variant (stance × feet) in the Library, then a trick, and the
  clip targets one variant unless "Regular and goofy (auto mirror)" or the Advanced tab is used. The concepts
  (RGS / SWS / FKS / NLS, regular / goofy, mirror, "record once in game") are not obvious to a non-skater.
- Many options live in Inspector tabs and checkboxes; the timeline shares the bottom of the window with a lot of
  controls; the 3D view is one panel among others.
- The audit of 2026-11-25 (`_archive/docs/audit/`) and the ReSkate study (`notes/STUDY-RESKATE-EDITOR.md`) list the
  remaining issues: few timeline rows at 720 p, hidden helpers, no first-run tour.

### 6.5 Open questions for the user (to answer before coding)
1. Target screen sizes (1280×720 minimum?) and whether a dark "pro" theme with a new font is wanted.
2. Is a dependency change acceptable (ImGui docking branch) or must layouts stay with our own splitters?
3. For tricks / stances / feet: a visual matrix (trick × stance × feet with recorded / clip status) as the entry
   point, or a "pick a trick, then see its four stances" flow?
4. Order of work: shell and theme first, or the trick / stance / feet flow first?
5. Should Creator / Studio stay as it is, or become a different density model?

### 6.6 Suggested approach (to confirm with a short plan in chat)
1. Split `app.cpp` into panel files with no visible change; user checks the editor still works.
2. Visual foundation (theme, spacing, icons drawn by code, no external assets without licence check).
3. Viewport-centred layout with the timeline as a first-class panel.
4. New trick / stance / feet navigator (editor-only; uses existing figures and clip triggers).
5. Inspector simplified around the selection; advanced options folded.
Each step: offline tests pass, agent screenshot when the user is not using the PC, then a user check list.
