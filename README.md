# Session Studio

Animation tools for **Session: Skate Sim**. Adjust your skater's pose in an editor, save, and the game plays it right
away, without restarting. The game's files are not changed.

This is an unofficial fan project, work in progress (version **v0.31**).

> [!NOTE]
> **Several clips switched on for the same trick** (same stance and foot) are drawn at random at each pop, never the
> same one twice in a row. Each keeps its own landing. In the clip list, switch **Game animation** on to let the game's
> own animation join the draw. Switch off the clips you do not want to see.

## How to install

1. Download `SessionStudio-v0.31.zip` from this repository's **Releases** page.
2. Extract the whole zip, close the game, and double-click **INSTALLER.cmd**.
3. Start the game, then open Session Studio.

**[Installation guide](INSTALL.md)**: install, update, uninstall, troubleshooting.

The package holds the game mod, the editor with its guides and its character model, and UE4SS 3.0.1 (installed only if
you do not have UE4SS).
It holds **no animation and no recording**: you do each move once in game, the mod records it on your PC, and you edit
from there.

You need the **Steam** version of the game on Windows (the Epic version has never been tried) and the
[Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vc14/vc_redist.x64.exe). SessionOpenMP is needed for
multiplayer only. ExpandedTricks is optional.

## What works

- **Works:** flip tricks (per stance and foot), manual and nose manual, on foot, pushing, crouch, landing,
  dismount, pose captures.
- **Experimental:**
  - riding poses, walking and running: not stable yet;
  - multiplayer through SessionOpenMP (tried with two players on an earlier version). To see another player's
    animation, you need a copy of their clip file: put it in `Mods\SessionAnimLoader\clips\others\` (any file name, a
    clip is recognised by its content). Clips in `others\` are played on that player's skater only, never on yours.
- **Not supported yet:** grinds and grabs. Powerslide is not checked.

A game update can stop the mod from hooking in. It then refuses without breaking anything: wait for a new version.

## Learn

- [Getting started: your first clip](SessionStudio/docs/Getting-Started.md)
- [Reference for creators](SessionStudio/docs/Reference.md)

## For modders

| Part | Folder | Role |
|---|---|---|
| Native mod (C++) | `SessionAnimLoader/Native` | plays the clips on top of the game's animation |
| Lua mod | `SessionAnimLoader/Lua` | reads the game state and passes it to the native mod |
| Editor | `SessionStudio` | creates and edits clips (`.sanim` files) |
| Installer and packaging | `installer`, `tools` | the release package |

[Build, test, package and how it works](DEVELOPMENT.md).

## Thanks

A big thank you to **matsix** ([SessionOpenMP](https://github.com/matsixx/SessionOpenMP)) and **Portman2**
(ExpandedTricks). Their work helped us a lot: Session Anim Loader would not be what it is without it.

## Licence and credits

Copyright (C) 2026 Ezear-Dev. Session Studio and Session Anim Loader are free software under the
**GNU General Public License version 3** (see [LICENSE](LICENSE)): you may use, study, change and share them; if you
share a changed version, share its source code under the same licence. No warranty.

- SessionOpenMP (matsix): UE4SS ABI header
- ExpandedTricksNative (Portman2): hook method
- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) (MIT), [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) (MIT)

Full notices: [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

Session Studio is inspired by Skate Style's Animation Studio (ideas only, no code nor assets).
This project is not affiliated with the developers or publisher of the game. This repository contains no game file.

**Character model:** the release package also holds `body.glb`, the character shown in the editor's 3D view.
It is not part of the program and is not covered by the GPL: it is included with permission, and all rights remain with its owners.
