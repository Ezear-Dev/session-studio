# Session Anim Loader (Lua mod)

UE4SS Lua half of Session Anim Loader. It drives `SessionAnimLoaderNative`.

- Install: `Mods\SessionAnimLoader\Scripts\main.lua`, `enabled.txt` and the `clips\` folder.
- The folder name must stay `SessionAnimLoader`: the DLL only gives its `ASN_*` functions to the mod of that name.

In the per-frame hook it adopts the player's skater, sends the mesh and bone table to the DLL, reads the animation
state every frame, rescans the clips folder every second, and handles multiplayer through SessionOpenMP.

| Key | Effect |
|---|---|
| Ctrl+F3 | mod on / off (starts on) |
| Ctrl+F4 | capture the game's pose into `clips\captures\` |

A broken clip file stops nothing: the log gives the error and the last good version keeps playing.
Logs: `[SessionAnimLoader]` lines in `UE4SS.log`, and `SessionAnimLoader.native.log`.
See [the reference](../../SessionStudio/docs/Reference.md) for the clip format.
