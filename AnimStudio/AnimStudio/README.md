# AnimStudio (Lua mod)

UE4SS Lua half of AnimStudio. It drives `AnimStudioNative`.

- Install: `Mods\AnimStudio\Scripts\main.lua`, `enabled.txt` and the `clips\` folder.
- The folder name must stay `AnimStudio`: the DLL only gives its `ASN_*` functions to the mod of that name.

In the per-frame hook it adopts the player's skater, sends the mesh and bone table to the DLL, reads the animation
state every frame, rescans the clips folder every second, and handles multiplayer through SessionOpenMP.

| Key | Effect |
|---|---|
| Ctrl+F3 | studio on / off (starts on) |
| Ctrl+F4 | capture the game's pose into `clips\captures\` |

A broken clip file stops nothing: the log gives the error and the last good version keeps playing.
Logs: `[AnimStudio]` lines in `UE4SS.log`, and `AnimStudio.native.log`.
See [the reference](../AnimStudioEditor/docs/Reference.md) for the clip format.
