# PROJECT_STATUS.md — real state of the software

Snapshot of **2026-11-26** (Git `7610976`, mod v0.24, clip format `sanim` version 1, game: Steam build of November
2026, UE4SS 3.0.1). **2026-11-27:** mod **v0.25** (real board in the recordings, §4) is installed, **checked in game by the
user**; later the same day mod **v0.28** was installed (late tricks, trick names, legs after the catch: §4 gives what
was checked for each); the v0.24 results below still describe what was checked in game. Update this file after each meaningful change.

**Evidence levels used below**
- **GAME ✔** — checked in game by the user (date and source given).
- **OFFLINE ✔** — covered by `asn_tests` (logic only; says nothing about what the player sees).
- **CODE** — present in the code, no user test result found in the notes.
- **✘** — tried and did not work, or parked.

## 1. Build and test state

| Item | State | Evidence |
|---|---|---|
| Offline tests | **2 FAIL out of 19133 checks** (the two board / shoe checks on real recordings, §4) | `test.cmd` run on 2026-11-27, release preparation (v0.25: ALL PASS 18874, before the new recordings) |
| Clips in the game folder | 27 loaded, 0 refused | `asn_tests --check-clips <game clips folder>`, same run |
| DLL + editor build | `dist\main.dll` and `dist\SessionStudio.exe` built 2026-11-27 (last build: legs in the end lock) from the v0.28 sources (installed, row below) | `build.cmd` OK, `--selftest` exit 0; the editor only displays the mod version read in the log (no expected number) |
| Installed on the user's game | **v0.28** DLL + Lua, 2026-11-27 (hashes equal to the build); later in game checks: §1b; rollback: `_archive\backups\installed_before-v0.28_2026-11-27`. Before: **v0.27**, user test 16:25-16:33 (§4); its rollback: `_archive\backups\installed_before-v0.27_2026-11-27`. Before: **v0.26**, user test OK (§4); rollback: `_archive\backups\installed_before-v0.26_2026-11-27`. Before: **v0.25**, user test OK (before that: v0.24 with 122 clip files, 2026-11-26 20:47) | v0.25: user test and game log, 2026-11-27 (§4). v0.24: `notes/HANDOFF.md`: user test OK, hooked behind ExpandedTricks, editor saves reloaded, Ctrl+F3 works |
| Editor self-test | new folders found, DLL and Lua present | `HANDOFF.md`, 2026-11-26 |
| Multiplayer on v0.24 | **not tested** | — |

The check count depends on the machine: tests on real game clips are skipped when the game folder is missing
(earlier notes quote 10842 or 4924 checks for older states).

## 1b. First release, v0.28 (2026-11-27: declared the official release by the owner, committed and pushed; **no GitHub release page yet**)

User decisions: GPL-3.0 for everything; release number = mod v0.28; a zip with `INSTALLER.cmd` that installs UE4SS only
when it is missing; the installer asks where to put the editor; no clip and no recording shipped.

| Item | State | Evidence |
|---|---|---|
| Package `release\out\SessionStudio-v0.28.zip` + `-source.zip` + `SHA256SUMS.txt` | built | `tools\make-release.ps1`: file names read back from both zips, forbidden-file and personal-data checks OK |
| Packaged mod | the **same files as installed** on the user's game | DLL `FCFA6D0D…` and Lua `4CDBCC68…` hashes equal. The rebuild from today's sources differs from that DLL by 4 bytes (offsets of the kind a build timestamp takes); the tested file was put back in `dist` and shipped |
| UE4SS in the package | official `UE4SS_v3.0.1.zip` (downloaded with the user's OK), loader only | `UE4SS.dll` / `dwmapi.dll` have the same SHA-256 as the user's installed files; the packaged settings have the same values (comments ignored). **A fresh install with the packaged UE4SS has never been run in game** |
| Installer (`installer\`) | OFFLINE ✔ on made-up game folders, with `-Yes` | fresh install, update (clips, `_studio.json`, UE4SS settings and editor settings kept; old DLL backed up), unknown / 3.0.0 / newer-layout UE4SS and leftover `dwmapi.dll` refused, 3.0.1 other build accepted, wrong folder, unextracted zip. The Steam search function alone finds the real game folder (nothing installed). **Not tested:** the questions, the folder windows, the desktop shortcut, a double-click on a downloaded zip (Windows warnings), a PC without the VC++ runtime |
| Editor icon and version details | in the exe | version 0.28.0.0 read from the exe; icon extracted from it at 256 / 48 / 32 / 16 px and looked at. **Title bar and taskbar icon: user check pending** |
| Source zip | builds | DLL and editor built from the extracted zip alone (VS 2022) |
| Docs | `README.md` rewritten, new `INSTALL.md`, `THIRD-PARTY-NOTICES.txt`; guides updated | not read by the user yet. README's "download from Releases" is true only once a release is published |
| Tests | **2 FAIL out of 19133** (the two board / shoe checks on local recordings, §4) | `test.cmd`, 2026-11-27 |
| `dist\SessionStudio.exe` | the new build with the icon (same file as in the package) | replaced once the user's editor was closed; `--selftest` of a copy of that build exit 0. Previous binaries: `_archive\backups\dist_before-release-v0.28_2026-11-27` |

**User test 1 (11-27, installer run on the real game, questions answered by hand):** it ran to the end, the Welcome card
shows five green dots and the icon is in the title bar (user's screenshot). Taskbar / Alt+Tab icon and the in-game
check: no report yet. Two problems came out, both fixed the same day:
- The "game animation ... loaded" toast was drawn over the Welcome card: it is now hidden while the card is open
  (build OK, `--selftest` exit 0; **not checked on screen**).
- The installer put the editor into the repository's `SessionStudio` source folder (a folder of that name was used as
  it was); the packaging check then stopped on its settings file. The installer now refuses a `SessionStudio` folder
  holding other files (its own backups and settings excepted, so `Documents` still works). The five stray files were
  deleted at the user's request; the desktop shortcut then pointed to nothing.

Package rebuilt after these fixes (new editor build; mod files unchanged): installer tests, now 12 scenarios, all pass.
**Clean-install test: not run yet**; local helper in `notes\test-clean-install\` (puts UE4SS and `Mods` aside and back;
tried on a made-up folder only, before-fix binaries: `_archive\backups\dist_before-toast-fix_2026-11-27`).

**User requests before test 2 (11-27), done:**
- The editor is always installed in a `SessionStudio` folder created inside the folder chosen (an existing editor is
  updated in place, and proposed first).
- **`UNINSTALL.cmd`** (package, and a copy in `Win64\SessionAnimLoader_uninstall\`). The installer records what it does
  (`Win64\SessionAnimLoader_install.json`); the uninstaller restores the state before the first install, keeps UE4SS
  when other mods use it or it changed, keeps files changed since, **moves** clips and recordings to
  `Documents\SessionStudio\clips-after-uninstall_<date>\`, and does nothing without a record. OFFLINE ✔ on made-up
  folders (12 cases, folder snapshots identical before / after; installer: 12 scenarios; all pass). **Never run on a
  real game.** Not known: whether UE4SS creates other files in game that would stay behind (test 2's helper lists
  them); the questions and the folder windows; the shortcut removal. The user's own current install has no record, and
  an install made over it will treat the mod as "there before": uninstalling keeps it.
- Thanks to matsix (SessionOpenMP) and Portman2 (ExpandedTricks) in the README, the notices and the package README.

**User's test with the new package (11-27, "install: OK, try: OK"; uninstall not run yet).** The files show it ran on
the **normal setup**, not on a game without mods (no folder of the set-aside helper, all the user's mods in place): the
record has no UE4SS entry and the UE4SS files keep their old dates (**an existing UE4SS stays untouched at install:
checked on the real game**), the mod files are marked as there before, the editor is in `Documents\SessionStudio`, and
`UE4SS.log` of the later session shows the mod v0.28 loaded. **The clean install with the packaged UE4SS is still
untested.** Two more offline cases (the player's own UE4SS build, with and without other mods): identical before /
after install, update and uninstall (uninstaller tests now 14 cases, all pass).

**First real uninstall (11-27, user, output sent; disk checked afterwards): as planned.** On the normal setup: editor
files, desktop shortcut, uninstaller copy and record removed; `Documents\SessionStudio\backups` kept (118 files); UE4SS
files with their old dates, all other mods and the clips folder in place; the mod's four files kept (there before the
first installation), identical to the package. **Not yet reported:** that the game still starts with the mods after it.
Not covered by this run: removing a mod or a UE4SS the installer added, and moving clips.

**Clean-install test, GAME ✔ (11-27 19:00-19:07: user's run and uninstaller output; logs and disk read afterwards).**
This supersedes the "never run" notes above for the fresh install and the uninstall. Set-aside, then the package: its
UE4SS 3.0.1 started with only our mod (native log: ExpandedTricks absent, "installed ... chained on the engine"; Lua
v0.28 loaded, SessionOpenMP absent). 70 bones, board found, moves recorded with their board, replay stand-by, a
KickFlip clip loaded and played (applied 1452 frames), Ctrl+F3 OFF 19:05:43 / ON 19:05:50, 0 faults, no refusal.
`UNINSTALL.cmd`: removed the mod, the UE4SS it had installed, `mods.txt`, the editor, the shortcut and its own copy,
and moved the 13 clip files to `Documents\SessionStudio\clips-after-uninstall_...` (checked there). Restore: the
helper's list of new items in the game folder is **empty** (nothing left behind; the two logs existed before the test),
and the user's setup is back (mods, clips, package DLL). Limits: the user's own PC (VC++ runtime present, `dxgi.dll`
of Unreal Mod Unlocker still loaded, zip built locally so no download warning); regular stance only.

Open before publishing: a first start without the points above (another PC, downloaded zip); goofy; multiplayer; a
clip copied to another PC. Older list: the in-game test of a clean install; `--check-clips`; with / without ExpandedTricks and a clip
copied to another PC (`notes/RELEASE-CHECKLIST.md`); goofy mirror and replay were checked on older versions only for
parts of this; multiplayer not tested since v0.21. "Version 3 only" or "or later" is not decided (the texts say
version 3). The repository is private: the GitHub release page and the visibility are the user's decisions.

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

- **Trick / landing seam** (user request 2026-11-27, **OFFLINE ✔; user check OK**: "no jump", answering the question
  "in the editor and in game"; the individual check steps were not reported one by one): the user does not need an
  exact paste, only a seamless junction. With an unlocked landing on the trick's timeline, its start is now **linked** to
  what is seen at the trick's end (`EditorDoc::LinkStartToTrick`, called every frame and before saving; keys at 0 only,
  no undo step, locked there; also for an existing landing clip, which then shows as not saved if its start differed).
  The unlocked landing in memory also counts as landing cover, so a new trick's end can be posed before saving. The
  clipboard now follows the playhead across the two parts; paste still pastes the change, not the visible pose.
  `test.cmd`: 2 known FAILs of 19199, 6 new tests pass; build OK; `--selftest` exit 0; in game not checked. `dist\SessionStudio.exe`
  is that build (replaced once the user's editor was closed), the same file as in the package. Code reading: the
  player applies the pelvis offset with the layer weight only, not the leg weight, as the link assumes; the cross-fade
  of the two clips at touchdown is not checked. The "Record again" fix
  above was confirmed by the user. Causes of the paste report, measured with a temporary test: (1) each part of the two-part timeline is its own `EditorDoc` with **its own
  clipboard**: a copy made in one part cannot be pasted in the other, which pastes nothing or an older copy of its own;
  (2) the clipboard holds key values, i.e. the change added to the game's pose: pasted at another instant the visible
  pose differs by as much as the game's pose moved (8 frames of a 1 deg/frame figure: 8.0 deg). (2) is the documented
  design of the 11-26 copy / paste.

- **Trick + landing timeline: "record again" leaves the old landing** (user report 2026-11-27; **fixed in the editor,
  user check pending**: `App::ReloadOtherPart` reads the other part again too; the game's own landing (locked, never
  written) is rebuilt, a landing clip of the user keeps its keys, a trick that had none gets one. Interface code, no
  offline test). With it, on the user's request, a **Record again** button in the top bar next to Save (variant shown;
  "Do it in game" while asked; disabled when not recorded yet); not seen on screen, 1280x720 not checked. Build OK,
  `--selftest` exit 0, `test.cmd` 2 known FAILs of 19133. `dist\main.dll` kept as the tested one. Previous binaries:
  `_archive\backups\dist_before-record-again_2026-11-27`. Cause (code reading, never confirmed on screen before the
  fix): the mod writes the trick's recording with its ground tail again (log of the
  clean test: KickFlip rewritten each time, 57-85 frames), but `App::ScanFolder` reloads only the open part (`var_`).
  The other part (`comp_`, built by `BuildLandingPart` only when a clip is opened or made) keeps the old recording until
  the trick is opened again; a landing that was missing is not added either. Same code for every trick. The generic
  "Landing" state has a recording of its own, which a trick's "record again" does not touch (by design).

- **Late tricks (late flips): recording is unreliable** (diagnosis of 2026-11-27; mod unchanged).
  **v0.25 game session 15:07-15:09 (log + files):** six late pops, each 0.36-0.43 s after an `RGS_Ollie` pop; only
  `Late_BackFoot_Heelflip` and `Late_Front_HeelFlip` were recorded (with board), at the second try.
  - **Cause 2:** a late trick reaches the ground 0.47-0.52 s after its pop and `FigureRecorder::Finish` drops an air
    part under 16 frames (0.5 s), silently. 0.467 / 0.483 s pops were dropped, 0.515 / 0.519 s ones kept (one 0.516 s
    pop was lost too, probably because a finished recording was still waiting to be written).
  - **Cause 1:** a late trick is a second pop in mid-air. If the first trick is being recorded and is at least 16
    frames old, it is saved cut short under its own name and the late trick is not started (older logs of a second
    player: `Ollie_SWS`, `HeelFlip_SWS`, 16 frames; same ordering in today's `figure.cpp`).
  - The two new recordings: `IsLanding` is on from the third frame, so the studio's markers are takeoff 0, catch 0,
    landing 0.07, ground 0.53 s (no flip part); landing part present.
  - Landing sync: jump height is measured from the late pop (log: pop 0, rise 0-3 cm, prediction 0.00-0.07 s).
    Effect on a clip in game: **not tested**.
  - Every title other than the late tricks is still the internal name (`Treflip`), not the game's `DisplayName`
    ("360 Flip", probe of 11-23).
  - **Fix, mod v0.26 (2026-11-27, user's choice: the late flip is recorded on its own): installed.** GAME ✔ (user test and log
    15:53-16:01: `Late_Front_KickFlip_RGS` recorded; `Ollie_RGS` and `Ollie_FKS` refused as "cut by another pop in
    mid-air", then a plain ollie recorded `Ollie_RGS`; `SWS_KickFlip` recorded while a late flip was waiting to be
    written; no "too short", fault or missing board. No grind was done). OFFLINE ✔, 4 new tests. In `figure.cpp`: a trick still in the air when another pops is no longer
    saved (so an ollie into a late flip saves no Ollie: a plain ollie records it); a recording popped in mid-air needs
    5 frames instead of 16 (ground pops unchanged); a finished recording waits in its own slot, so it no longer blocks
    the next move; a thrown-away trick recording now logs "move not recorded: ... (why)". A pop out of a grind is not
    treated as a mid-air pop (defensive: no grind in any log). The old test of "a new pop saves the cut trick" was
    changed on purpose. Clip format, figure format and network wire unchanged. `test.cmd`:
    2 FAIL out of 18933 checks (the two board / shoe checks on real recordings below). `build.cmd` OK, `--selftest`
    exit 0. The installed Lua differs from the new one by the version line only. Previous `dist` binaries:
    `_archive\backups\dist_before-v0.26_2026-11-27`. Landing sync and the missing flip phase are **not** changed.
- **Trick names read in game, mod v0.27 (2026-11-27, user request): installed, not tested in game**
  (OFFLINE ✔). At each pop the Lua sends `FlipTrick.DisplayName` (read with the probe of 11-23, never checked on late
  tricks); the DLL logs "trick name: ..." when new or changed and writes `clips\_trick_names.json` at its folder scan
  (merged with the earlier file). Title of a variant: name typed in the catalogue, else name read, else derived; card
  title: name read for the Normal stance. Whether `DisplayName` is what the user sees on screen is **to be checked**;
  then the two typed names can be removed. Clip, figure and network formats unchanged. `build.cmd` OK, `--selftest`
  exit 0. Previous `dist` binaries: `_archive\backups\dist_before-v0.27_2026-11-27`.
- **Result of the v0.27 test, and late-flip cards (2026-11-27)**: **GAME ✔ for reading the names** (33 names in
  `_trick_names.json`, log 16:25 on). That `DisplayName` is word for word what the screen shows is **not confirmed**
  (the user did not send the names read on screen). The two names typed by hand were **removed**: "Switch Late Flip"
  had been put on the wrong variant (the game gives it to `SWS_Late_Front_KickFlip`). **User report:** the Fakie cell
  of the "Late Back Foot Heel" card was filled by the front-foot heelflip. Cause: the mod recorded under the right
  internal names, but in **switch and fakie** the game names a late flip with the **other foot** than its internal
  name (normal and nollie do not); checked on every read name that says a foot. **Fixed in the editor** (`WithStance`,
  `BuildLibrary`, drawer pick, copy to another variant, derived fallback titles); the mod, clips and files are
  unchanged. A clip's stance list in Advanced still follows the internal name. `test.cmd`: 2 FAIL out of 19091 (the
  known two), new tests pass; `--selftest` exit 0; `dist\SessionStudio.exe` replaced at 16:40 (previous:
  `_archive\backups\dist_before-late-cards_2026-11-27`). **User check pending.**
  - "Copy to another variant" takes the trick from the chosen variant (code reading: `SetClipVariant`). The five read
    names that say no foot ("Switch Late Flip", "Nollie Late Flip"...) are placed by the rule, not by their words.
  - **Assumption, goofy not tested:** the swap is the same for both feet rows. SessionTweaks pairs the internal
    `BackFoot` / `Front` names with assets named `RF` / `LF`, so for a goofy skater the swapped stances could be Normal
    and Nollie instead. No goofy late flip has been sent yet.
  - The DLL sources changed since the installed v0.27 (`trick.cpp`: `WithStance`, typed names removed) but nothing the
    mod uses: no version bump, nothing to install.
  - Open: the fakie late **heelflip** recordings have no landing part: at each recorded attempt the log goes to
    running / walking / on foot at the ground contact (looks like a bail; to be confirmed by the user).
- **"Move the legs after the catch", mod v0.28 (2026-11-27, user request): installed, GAME ✔** (user: it works, in
  the studio and in game; log: v0.28 session from 17:03, no fault or refusal; `Ollie_RGS_regular.sanim`, option on,
  reloaded 4 times 17:11-17:14, with 17 `RGS_Ollie` pops afterwards. A late-flip clip saved at 17:17 was deleted at
  17:18: its option is not on record. Older late-flip clips were deleted in the same session, so "an old clip plays as
  before" was not checked on them. Multiplayer not tested.)
  (OFFLINE ✔). New optional clip field `play.legsAfterCatch` (format change agreed by the user: absent = off, so
  existing clips are written and played as before; on by default for new late flip clips only). Player: the leg weight
  of such a clip stays at 1 after the catch (also sent as such to other players; wire format unchanged). Editor: switch
  in Clip settings, legs no longer frozen. The user saw late flips raise the catch almost at once. Whether the feet
  then look right on the board in game is **not known**. Mods older than v0.28 ignore the field. The "Go to the landing"
  / "Go to the trick" buttons were removed from Clip settings (user request). The user reacted positively to the late-trick cards
  (screenshot, 11-27); the three checks asked for (on-screen wording, fakie heelflip bail, goofy) were not answered. The
  end lock and the end-pose warning cover the legs of such a clip too (they skipped trick legs before). `build.cmd` OK, `--selftest` exit 0, `dist\SessionStudio.exe`
  replaced at 16:55 with game and editor closed (previous binaries: `_archive\backups\dist_before-v0.28_2026-11-27`).
- **Editor trick list (2026-11-27, user request):** the "Seen in game" collection is removed; the list is the
  catalogue alone (`trick.cpp`), so a trick missing from it has no card (its files stay; its clips stay in My clips).
  The real names that were only in that collection were added to Flips / Shove-its; the 8 `Late_LF/RF` entries (never
  sent by the game) are replaced by the 6 late names seen in logs, the four flips titled as the user reads them
  ("Late Back Foot KickFlip"...). Three more never-sent ollie asset names (`Nollie`, `ShittyNollie`,
  `ShittySwitchOllie`, the last one inferred) are replaced by the real `Ollie` / `ShittyOllie` entries. Seen since: `SWS_Late_BackFoot_KickFlip`, `SWS_Late_Front_KickFlip`,
  `FKS_Late_Front_KickFlip` (log) and `SWS_Late_BackFoot_Heelflip` (recording). The game names each stance on its own:
  the catalogue can hold the on-screen name of a variant (`onScreen`). From the user: `SWS_Late_BackFoot_KickFlip` =
  "Switch Late Flip"; `FKS_Late_Front_KickFlip` = "Fakie Back Foot Late Flip" (mapping from the log: the only fakie
  late flip ever sent). The other switch names are **not given yet** (derived name shown). Not checked on screen.
- **"chest follows the hand" is off by default** (user request 2026-11-27; not a saved setting). Not checked on screen.
  The screen is **not checked** by the user.
- **Shoes not flat in the 3D view (user report with screenshots, 2026-11-27)**: **fixed in the editor, user check
  pending.** Cause (measured): the sole's "up" was taken from the torso axis on the first frame of the `OnFoot`
  recording; the one made at 15:06 starts leaning 46.1 deg (the older one 2.6 deg), so every shoe leaned 46 deg
  (31-84 deg against the recorded deck). Now the sole comes from the skeleton's reference toe offset (`mannequin.cpp`;
  no pose needed). **Still open:** with that rule the front foot is within about 6 deg of the recorded deck but the
  right (back, regular) foot is about 16-35 deg off it on the board (about 34 deg at the start of `Ollie`). That is in
  the recorded bone rotation. The recorder samples the pose **before** calling the rest of the chain (code reading:
  `SafeApply` then `g_prev`), so what ExpandedTricks or SessionTweaks change afterwards is not in recordings. Not known whether the game changes that foot after the recorder reads it (candidates, not
  checked: SessionTweaks foot fixes, ExpandedTricks) or shows it like that; asked the user to look in game.
- **Tests and build (2026-11-27, after both changes)**: `test.cmd` **2 FAIL out of 18923 checks**, both in
  `board_and_shoes_on_the_real_figures` on recordings where the right foot is not flat (`Riding` made 15:18: right toe
  4.6 cm above the grip; `OnFoot` 15:06: right heel 2.5 cm up). The count moves with the user's recordings. New tests
  pass. `build.cmd` OK, `--selftest` exit 0 (31 tricks). `SessionStudio\dist\SessionStudio.exe` was replaced at 15:25
  with the editor closed (previous build: `_archive\backups\dist_before-trick-list_2026-11-27`). Mod not installed;
  `dist\main.dll` restored to the installed build.
- **Multiplayer versions:** v0.24 changed the channel to `animloader.clips1`; a v0.21-v0.23 player and a v0.24
  player cannot see each other. The friend still has v0.21. `_archive/tester-packages/` describes old versions
  (v0.18-v0.21); a new package must be written from scratch.
- **Peer stutters:** small stutters remained on the peer's screen in v0.21; the v0.22 fix is only simulated.
- **Game updates** can move the hook offsets: the DLL then refuses to install (by design, no crash).
- **Unexplained crash** on 2026-11-23, ~2 min after a map change (`ACCESS_VIOLATION`,
  `SessionGame-Win64-Shipping.exe+0x3233233`, no mod on the stack). Not reproduced.
- **Landing log artefact:** lines "landing: lasted 6-8 s" when the game is paused or in replay (cosmetic, not fixed).
- **Editor board and game board:** the skeleton's board bones do not flip; the real board is a physics actor. Since
  v0.25 it is recorded with the moves (older recordings only have the approximate board).
  **Real board, v0.25 (2026-11-27)** — the user chose to record and *show* it (read-only; `.sanim` unchanged):
  - **Probe, GAME ✔** (user test, regular stance; temporary mod removed afterwards; measures in local
    `notes/probes/results`): the `Flipper` component of the `Skateboard` actor is the visible deck (kickflip 357°,
    heelflip -356° about its X, deck bone under 8°; riding at (0, 0, 7.0) in mesh space; carried on foot). No hitch felt.
  - **Recording and display: GAME ✔** (user test 2026-11-27 14:17-14:25, regular: "everything seems to work"). Log and
    files: board found at load, KickFlip (1 time) and Treflip (2 times) recorded with it, no `board: lost`; one refusal
    "the board is away from the skater" at 14:18:39, recordings after it had the board. KickFlip file: 357° about the
    deck's X, starts and ends at about (0, 0, 7). Riding, on foot, heelflip and shove-it were **not** recorded again
    in that session. Offline: `test.cmd` ALL PASS (18849), `build.cmd` OK,
    `--selftest` exit 0; DLL + Lua v0.25 installed (previous ones in `_archive/backups/installed_before-board_2026-11-27`).
    The editor view is checked only by the user.
  - Still to check: heelflip / shove-it / riding / on foot recorded again, the trick + landing timeline after a
    shove-it; **goofy never measured** (a scaled mesh is refused, so goofy would record no board); the 1.7 cm offset of the drawn board is derived, not
    measured, and trucks / wheels are drawn rigid (not measured).
  - 2026-11-27, at the user's request: the 76 recordings without a board were moved out of the game's `_figures` to
    a local backup, which the user then asked to delete the same day (they can re-record any time). Only
    `KickFlip_RGS_regular` and `Treflip_RGS_regular` are left; the mod records a move again the next time it is done.
    `test.cmd` then ALL PASS (18847: some tests on real recordings are skipped), `--selftest`: 2 recorded moves, 0 clips
    (no `.sanim` was in the clips folder; the agent did not touch clips).
  - The user validated the steady trick/landing timeline and the click-in-the-void deselection in the editor
    ("Niquel", 2026-11-27).
  - Possible lag of one game frame between the board (read by the Lua) and the pose (taken by the hook): not measured.
- **Editor preview ≠ game:** physics, SessionTweaks' `body_feel` and game blending apply on top. The live-preview
  idea (probe C0) was a **no-go** on 2026-11-26 (camera locked on the board, game pauses without focus).
- **Agent limits:** simulated mouse clicks do not reach the ImGui window; mouse-driven UI is verified only by the user.
- **Editor often left open** by the user: new builds cannot overwrite `dist\SessionStudio.exe` while it runs.
- **Out-of-date docs** (identified for this document, not fixed): `SessionStudio/docs/Reference.md` still says the
  editor "shows bones, not the skinned body" and that multiplayer is "v0.19, 2-player test pending";
  `notes/HANDOFF.md` mentions a `tester-package/` folder that no longer exists at the root (it is in
  `_archive/tester-packages/`); `notes/RELEASE-CHECKLIST.md` still uses AnimStudio names.
- **Licence:** GPL-3.0 for the whole project (user's choice, 2026-11-27).
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

## 6. UX/UI redesign project (started 2026-11-26 on branch `redesign-ui`, merged into `main` 2026-11-27)

**Current state (2026-11-26):** design v2 approved by the user — two full-screen workspaces, *Browse* (library) and
*Animate* (full-width stage + full-width timeline, floating tools, contextual inspector card, clip settings panel);
the user also asked for care on the stage mannequin (hands with fingers, feet). Detailed spec: local
`notes/REDESIGN-PROPOSAL.md` (v1, a restyle of the four-panel layout, was rejected).
| Step | State | Evidence |
|---|---|---|
| 0. Split `app.cpp` into panel files, no visible change | done, GAME ✔ (editor) | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18607); user: 5 checks OK, 2026-11-26 (commit `1e96473`) |
| 1. Theme: palette, style, fonts, icons, widgets; colours of the existing panels moved to the theme (layout unchanged) | done, GAME ✔ (editor): user 5 checks OK, 2026-11-26 | build OK; `--selftest`: "fonts: Segoe UI yes, Semibold yes, icons SegoeIcons.ttf 31/31" (glyphs exist; that they show the intended pictures is not verified); `test.cmd` ALL PASS (18607) |
| 2. Shell: Browse / Animate workspaces, light top bar (variant, clip, save state, game pill + popup, Save, undo / redo, ... menu), no status strip, Library as Browse page or drawer (B), toasts, Auto key in the transport, shortcuts Tab / B / Ctrl+N / Ctrl+O | done, GAME ✔ (editor): user 7 checks OK, 2026-11-27 | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18607). Guides (`Getting-Started`, `Reference`) still describe the old layout: step 8 |
| 3. Stage: view fills its panel (gradient, fading 5 m grid, contact shadow, rounded picture); floating tools as child windows (tools pill, View menu, camera card, shortcuts help); selection badge under the tools; banners bottom left; toasts bottom centre; mannequin light and colours. **First user check (2026-11-27)**: mannequin OK, View menu OK, Top OK; reported: feet through the floor, body picker useless, Ghost useless in the skeleton view, Center / Reset / 1/4 turn not right, tool buttons and banners not working. **Fixed**: floor under the lowest point of the game's animation (`GroundHeight`; measured on the real figures: KickFlip -41 cm, Ollie -30, Landing -16, OnFoot -0.25), body picker removed (Studio bone list back in the Inspector), Ghost only in the body view, Reset frames the posed skater (`FrameBox`, also at the first view), Center comes closer, red / green floor axes + "front" so a camera turn reads. Tool buttons: user retest 2026-11-27 "they work in fact": the playhead was on the locked start frame, where nothing can be posed (no change made). Banners: confirmed on the user's screenshot (locked start). **Second user check**: the skater floated at the start (floor at -41 cm). Recordings measured: ground reference at 0 (root bones and board), feet ~11 cm up on the board, tricks dip to -40 cm only for a few frames (pop). User: "maybe remove the floor?" -> the floor is now an option, View > Floor, off by default, at the game's height 0; orientation axes (front / up) always in the top-left corner; `GroundHeight` removed; stage background darker (user request) | done; user asked to commit before checking the darker background | build OK; `--selftest`: stage shaders OK; `test.cmd` ALL PASS (18662, new tests `view_frame_box_fits_and_centres`, `mannequin_ground_height_is_under_the_soles`, `mannequin_ground_on_the_real_figures`) |
| 4. Inspector card: no Inspector column (the stage takes the whole width); a card at the right of the 3D view (child of the stage; the view canvas stops before it, so the skater is centred in the free part and no ring sits under it) shows the selection with its tool in the header; the timeline's "Clip settings" shows the clip settings in the card (Studio: + Advanced); "Inspector" toggle on the View card, close button in the card (saved `ui.card`); Creator / Studio only in the ... menu. **User check 2026-11-27**: all 5 points OK; asked: "Clip settings" next to "Inspector" on the view (moved from the timeline), card buttons and boxes looked like text (card now has lighter buttons and darker fields), shoulder warning too intrusive (now a light tint on the upper arm only); a rename to "Options" was tried and reverted at the user's request. User: "j'adore" | done, GAME ✔ (editor) | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18649) |
| 5. Timeline: no title row; one header line (left: + key, - key, interpolation segmented, paste, selected keys; centre: transport, loop, speed, Auto key, large time + frame; right: end-warning chip with the list and fixes, zoom, Fit, view menu with compact / empty rows / enlarge, help); blue Game row; phase ribbon under the ruler (takeoff, flip, catch / in the air, landing, ground; loop / dismount) with tooltips; phase lines in the rows without labels; limb colour bars on the rows. Every mouse rule unchanged. **User check 2026-11-27**: header, keys, ribbon, mouse rules, view menu OK; warning chip not seen (it only shows when an end is not back: a new clip has none). The ribbon then showed no air part on a KickFlip recorded without catch / ground events: it is now built from the same `TrickMarkers` as the yellow band of the rows (pop, flip, in the air, landing, ground; a flip tick when there is no catch); ribbon confirmed by the user | done, GAME ✔ (editor) | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18649) |
| Landing on the trick's timeline (user request during step 5): a trick clip and the landing clip of that trick on one timeline, two files. The landing comes after the ground contact (violet line), locked by default (the game's landing: game pose shown, nothing posed, keys hidden); "Lock the landing" in Clip settings; unlocked = your landing clip, saved by Save with the trick; locking again turns an existing landing clip off (kept). Each part keeps its own undo history; playback goes from the trick into the landing. Mod, Lua and clip format unchanged. An existing ENABLED landing clip opens unlocked (locked would mean the game plays its own landing, which would be false); user agreed 2026-11-27 | done, GAME ✔ (editor + game): user "tout fonctionne correctement", 2026-11-27 | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18690, new `two_part_split_and_join`, `two_part_playback`). The interface parts are checked only by the user |
| 6. Browse page: collections on the left (every trick and state, by category; My clips; Captures; search Ctrl+K; "Recorded" and "Has clips" filters), one card per trick or ground state with its stance x feet dots (green = recorded in game, orange centre = a clip of yours is on, white ring = the variant being edited), detail panel on the right (big variant grid, what plays in game on that variant, its clips with on/off switch, "Animate this variant", "New clip for this variant"). My clips = sortable table with filters (all, on, off, problems, the variant being edited); Captures = a grid. Double-click a card = animate it. The old Library stays as the Animate drawer (B). Card data and the "which clip plays" rule moved to `model/library` (tested) | done, user OK (11-27) | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18663, new `library_cards_count_variants_and_clips`, `library_filters`, `library_playing_clip_follows_the_player_order`). The page itself is checked only by the user |
| 7. Dialogs, welcome, Preferences, shortcuts list: every dialog has one look (title in the title font, card colours, buttons on the right, Enter = main button, Esc = Cancel; Delete never on Enter); "Open a clip" has a search field; "Copy to another variant" uses segmented buttons; the welcome is a card with the setup check (count to fix) and five numbered steps (Browse wording). New: Preferences (Ctrl+, or ... menu: Creator / Studio, text size Small to Larger, clips folder, backups, welcome at start) and the Keyboard and mouse list (F1 or ... menu). What each button does is unchanged | done, **user check pending** | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18663). Interface only, checked only by the user |
| 8. Polish and guides: the view messages at the bottom left start right of the Library drawer when it is open (they were hidden under it); `Getting-Started` and `Reference` rewritten for Browse / Animate, the game pill, Clip settings, Preferences and F1. Pose thumbnails on the Browse cards: not done (optional, left for later) | done, **user check pending** | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18663). The messages and the guides are checked only by the user |
| 8b. Library drawer looks like Browse (user request 11-27): title and close button, the move being edited with the Browse variant grid (click a cell = that variant), segmented Tricks / Clips / Captures, Tricks organised like Browse (collection list Everything / categories with counts, "Recorded" and "Has clips" filters shared with Browse, the Browse cards in a grid that fits the drawer width; click = that move in the same stance and foot, a dot = that variant), clip rows with switch and "..." button, captures as small cards. The grid and the card list are shared with Browse (`VariantGrid`, `LibraryCards`) | done, **user check pending** | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18663). The drawer itself is checked only by the user |
| 8c. Picking a trick (Browse or the drawer) opens the clip that plays on that variant (enabled, higher priority, then more specific), else the first clip made for it; before, the first clip of the list was opened even when it was off (user report 11-27). Rule `ClipToOpen` in `model/library` | done, **user check pending** | build OK; `--selftest` exit 0; `test.cmd` ALL PASS (18667), with a new test of the rule. Not checked in the editor by hand |

| Trick + landing: shorter landing, steady interface (user request 11-27): a trick and a new landing (or the preview of the game's) last 2 s together at most (landing at least 0.8 s, the game's minimum; saved landing clips keep their length). At the violet line the title bar, the Library drawer, the timeline rows (both parts merged), the other part's keys, the part chip and the Inspector card keep their place; no lock banner during playback; zoom works on the whole timeline | done, **user check pending** | `test.cmd` ALL PASS (18869, new `landing_length_...`, `rows_of_the_two_parts_...`), `build.cmd` OK, `--selftest` exit 0. The screen is checked only by the user |
| 3D view: a click in the void deselects the part (user request 11-27); a click on the body (or near a bone in the skeleton view) keeps it; a handle that is no longer shown no longer swallows clicks | done, **user check pending** | `test.cmd` ALL PASS (18874, new `point_on_the_body_or_in_the_void`), `build.cmd` OK, `--selftest` exit 0. The clicks are checked only by the user |

Sections 6.2-6.6 below are the preparation notes written before the design; where they differ from the spec, the
spec wins (e.g. §6.6 order).

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
- `app.cpp` was a single ~3100-line class: split into panel files on 2026-11-26 (step 0, see above).
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
