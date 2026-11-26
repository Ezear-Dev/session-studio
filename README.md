# AnimStudio

An animation studio mod for **Session: Skate Sim**. Adjust your skater's pose in an editor, save, and the game plays
it right away, without restarting. The game files are never modified.

Three parts work together:

| Part | Folder | Role |
|---|---|---|
| Native mod (C++) | `AnimStudio/AnimStudioNative` | plays the clips on top of the game's animation |
| Lua mod | `AnimStudio/AnimStudio` | reads the game state and passes it to the native mod |
| Editor | `AnimStudio/AnimStudioEditor` | creates and edits clips (`.sanim` files) |

## Status

Work in progress. There is **no ready-made download yet**: you have to build it from source.

- **Works:** flip tricks (per stance and foot), manual and nose manual, on foot, pushing, crouch, landing,
  dismount, pose captures.
- **Experimental:**
  - riding, walking and running: not stable yet;
  - multiplayer through SessionOpenMP (tested with two players). To see another player's animation, you need a copy
    of their clip file: put it in `Mods\AnimStudio\clips\others\` (any file name, a clip is recognised by its
    content). Clips in `others\` are played on that player's skater only, never on yours.
- **Not supported yet:** grinds and grabs. Powerslide is not checked.

## Requirements

- Session: Skate Sim on Windows
- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) 3.0.1
- SessionOpenMP, for multiplayer only
- ExpandedTricks is optional; AnimStudio installs behind it

## Documentation

- [Build and install](DEVELOPMENT.md)
- [Getting started: your first clip](AnimStudio/AnimStudioEditor/docs/Getting-Started.md)
- [Reference for creators](AnimStudio/AnimStudioEditor/docs/Reference.md)

## Licence and credits

The native mod (`AnimStudio/AnimStudioNative`) is licensed under the **GPL-3.0** (see [LICENSE](LICENSE)). The editor
builds on the native mod's core library (`AnimStudioNative/src`). No licence has been chosen yet for the parts outside
the native mod.

- SessionOpenMP (matsix): UE4SS ABI header
- ExpandedTricksNative (Portman2): hook method
- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS), [Dear ImGui](https://github.com/ocornut/imgui),
  [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)

AnimStudio is inspired by Skate Style's Animation Studio (ideas only, no code nor assets).
This is an unofficial fan project, not affiliated with the developers or publisher of the game.
