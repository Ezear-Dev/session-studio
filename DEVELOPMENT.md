# Development

All paths below are relative to the repository root.

## Requirements

- Visual Studio 2022 with the C++ tools, its bundled CMake, and a Windows SDK.
- `build.cmd` and `test.cmd` call `vcvars64.bat` from `D:\Microsoft Visual Studio\2022\Community`:
  edit that line to match your install.
- For the editor, two libraries that are not part of this repository:

```
git clone https://github.com/ocornut/imgui SessionStudio/third_party/imgui
git -C SessionStudio/third_party/imgui checkout c1aaac6
git clone https://github.com/CedricGuillemet/ImGuizmo SessionStudio/third_party/ImGuizmo
git -C SessionStudio/third_party/ImGuizmo checkout 105e4fd
```

Without them, the editor is skipped and the rest still builds.

## Build and test

| Command | Result |
|---|---|
| `SessionAnimLoader\Native\build.cmd` | builds into `%LOCALAPPDATA%\SessionStudio\build`, copies the DLL to `SessionAnimLoader\Native\dist\` and the editor to `SessionStudio\dist\` |
| `SessionAnimLoader\Native\test.cmd` | builds and runs the offline tests |

`build.cmd` generates `UE4SS.lib` from `third_party\ue4ss\UE4SS.def` when it is missing.
Some tests read real files from the game's clips folder at the default Steam path, and are skipped without them.

## Install (game closed)

In the game's `SessionGame\Binaries\Win64\` folder, with UE4SS installed:

| Copy | To |
|---|---|
| `SessionAnimLoader\Native\dist\main.dll` | `Mods\SessionAnimLoaderNative\dlls\main.dll` |
| `SessionAnimLoader\Lua\Scripts\` and `SessionAnimLoader\Lua\clips\` | `Mods\SessionAnimLoader\` |
| `SessionAnimLoader\Lua\enabled.txt` | `Mods\SessionAnimLoader\` |
| an empty file named `enabled.txt` | `Mods\SessionAnimLoaderNative\` |

The Lua mod folder must be named `SessionAnimLoader`. Install the DLL, the Lua script and the editor from the same version:
the editor reads the DLL's log and files. The editor can run from anywhere, with its `docs` folder next to it.

Logs, in the same `Win64` folder: `SessionAnimLoader.native.log` and `UE4SS.log` (lines `[SessionAnimLoader]`).

The editor's Help button opens the HTML version of the guides when present, else the Markdown one. To generate it:
`node SessionStudio/docs/md2html.js SessionStudio/docs`.

## How it works

**Pose hook**
- The DLL replaces the vtable slot `FinalizeBoneTransform` of the skater mesh (`vtable + 0x938`).
- Editable buffer: `TArray<FTransform>` at `mesh + 0x4b0 + i*0x10`, `i = *(int*)(mesh + 0x4f0)`.
- UE 4.27 `FTransform`: 48 bytes, component space.
- These offsets depend on the game build. After a game update the DLL may refuse to install.

**Other mods**
- ExpandedTricksNative takes the slot once, when the skater appears, and refuses anything but the engine function:
  Session Anim Loader always installs after it. Chain: Session Anim Loader, ExpandedTricks, engine (with SessionTweaks' pre-hook).
- The game's own trick animation stretches the shin in the air. Leg chains written by clips use the reference bone
  lengths, and a pass after the other mods restores them.

**Skeleton and states**
- 70 bones, parents before children. Board bones (`SKXX_SkateSkel_*`) start at index 58.
- States come from the animation instance `ABP_AMXX_Skater_C` (`IsInManual`, `ManualRatio`, `FlipTrick`...).
- `FootPositionType`: 1 normal, 2 fakie, 3 nollie, 4 switch.
- The game replay already records the clip layer, so the mod stands by during a replay.
- Mirroring works bone by bone. The editor mirrors its 3D view on Y, because Unreal is left-handed.

**Multiplayer**
- Each player sends its clip layers (clip identity, time, speed, weights) 15 times a second on the SessionOpenMP
  channel `animloader.clips1` (wire text `S2` + hex, `src/net.h`). Receivers play their own copy of the same clips on
  that player's skater.
- A clip's identity is its content (`ContentId`: FNV-1a 64 of the file without BOM and `\r`), never its file name.
- `clips\others\` (sub-folders allowed) holds other players' clips: loaded and found for peers, never chosen for our
  own skater, not shown in the editor.
- The receiver keeps its own clock per layer and pulls it softly toward the received values (`Player::ApplyRemote`):
  it never jumps back in time, and it snaps only on a big gap or a new layer.

## Rules for the Lua mod

- Everything runs in the per-frame hook `SkaterCharacterBase:GetSkateboardingAnimParams` (game thread).
- No `LoopAsync`, no `FindAllOf` while playing. Key handlers only raise a flag.
