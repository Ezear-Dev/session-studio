# SessionAnimLoaderNative (C++ mod)

UE4SS 3.0.1 C++ mod: the clip player. It layers `.sanim` clips on the **finished** pose of the player's skater.
It needs its Lua half, `Mods\SessionAnimLoader`.

- Hook: vtable slot `FinalizeBoneTransform` of the skater mesh. It installs behind ExpandedTricksNative when present.
- Safety: it only touches the mesh given by the Lua, checks the bone count every frame, runs under `__try`.
- Log: `Win64\SessionAnimLoader.native.log`, rewritten on every launch.
- Build: `build.cmd`; offline tests: `test.cmd`. See [the development notes](../../DEVELOPMENT.md).

## Licence

GPL-3.0. `third_party/ue4ss_abi/ue4ss_abi.h` comes from SessionOpenMP (matsix). The slot, offsets and chaining order
come from ExpandedTricksNative (Portman2).
