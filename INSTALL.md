# Installing Session Studio and Session Anim Loader

This guide covers installing, updating, removing and fixing problems. It is written for version **v0.30**.

You get two things:

- **Session Anim Loader**, the game mod: it plays animation clips on your skater.
- **Session Studio**, the editor: a separate program to create those clips. A player who only wants to play clips made
  by others never has to open it.

The game's own files are not changed. The mod is added next to them.

---

## Before you start

| You need | Notes |
|---|---|
| Session: Skate Sim on Windows, **Steam** version | tested with the Steam build of November 2026. The Epic version has never been tried. |
| **UE4SS 3.0.1**, the mod loader | included in the package. If you already have it, nothing changes. **Only 3.0.1** works. |
| **Microsoft Visual C++ Redistributable (x64)** | often already installed. If not: [download it from Microsoft](https://aka.ms/vc14/vc_redist.x64.exe). |

Optional:

- **SessionOpenMP**: only for multiplayer.
- **ExpandedTricks**: not needed. When it is there, Session Anim Loader installs itself behind it.

If you installed **SessionOpenMP**, you most likely have UE4SS 3.0.1 already, because its package ships it.

---

## Install

1. Download `SessionStudio-v0.30.zip`.
2. **Extract the whole zip** into a normal folder (right-click the zip, **Extract All**). Do not run anything from
   inside the zip.
3. Close the game and Session Studio.
4. Double-click **INSTALLER.cmd**.
5. Answer the questions:
   - **Game folder**: the installer finds the Steam install and asks you to confirm. If it does not find it, a window
     opens: choose the game folder (named `Session`).
   - **Editor**: a window asks where to put Session Studio. A `SessionStudio` folder is always created **inside** the
     folder you choose (the installer refuses one that already holds other files). Choose a folder you can write to, such as your Documents or another drive, **not** Program Files: the editor saves its settings
     next to itself. **Cancel** skips the editor.
   - **Desktop shortcut**: yes or no.

The files are not digitally signed, so Windows may show a warning before running them: for the installer, and again
the first time you start the editor. Only continue if the zip comes
from this project's own release page. You can compare the zip with `SHA256SUMS.txt`:

```
Get-FileHash .\SessionStudio-v0.30.zip
```

The installer is a plain text script (`files\install.ps1`): you can read it first. It downloads nothing.

### What the installer puts where

`Win64` is the game folder `...\steamapps\common\Session\SessionGame\Binaries\Win64\`.

| What | Where |
|---|---|
| UE4SS (`UE4SS.dll`, `dwmapi.dll`, `UE4SS-settings.ini`, `Mods\mods.txt`) | `Win64\`, **only if you had no UE4SS** |
| The native mod | `Win64\Mods\SessionAnimLoaderNative\` |
| The Lua mod | `Win64\Mods\SessionAnimLoader\` |
| Your clips | `Win64\Mods\SessionAnimLoader\clips\` (created empty) |
| The editor and its guides | the folder you chose |

---

## First start

1. Start the game and put the skater on the board. The mod hooks in by itself after about 10 seconds.
2. Open **Session Studio**. The **Welcome** card checks your setup: every line should have a green dot. A red dot tells
   you what to do.
3. **No animation and no recording is included.** Before you can edit a move, do it **once in game**: the mod records
   the game's original movement, on your PC. Do this once for each move, **per stance** (Normal, Switch, Fakie, Nollie)
   and **per foot** (Regular, Goofy). In the editor, a recorded move shows a green dot.
4. Make your first clip with the guide: `docs\Getting-Started.html`, next to `SessionStudio.exe` (or **Open the
   guide** in the editor).

In game: **Ctrl+F3** turns the mod on or off, **Ctrl+F4** captures the skater's current pose.

---

## Update

1. Close the game and Session Studio.
2. Extract the new zip into a new folder and run its **INSTALLER.cmd**.
3. The installer proposes to update the editor where it is: answer **yes**. Its settings are kept.

The installer replaces the mod's files and the editor's program and guides. It **never** deletes or replaces anything
in your `clips` folder. The previous mod files are copied to `Win64\SessionAnimLoader_backups\<date>\`.

Always update the mod and the editor **together**: the editor reads what the mod writes. For multiplayer, every player
needs the same version.

---

## Install by hand

If you prefer not to run the installer (game closed):

1. If you have **no UE4SS**: copy the content of `files\UE4SS\` into `Win64\`. If you already have UE4SS, skip this and
   keep yours.
2. Copy the two folders of `files\Game\Mods\` into `Win64\Mods\`. When updating, replace only
   `SessionAnimLoaderNative\dlls\main.dll` and `SessionAnimLoader\Scripts\main.lua`, and **leave your `clips` folder
   alone**.
3. Copy `files\Editor\` anywhere you like and run `SessionStudio.exe`. If it does not find the game, use **Choose the
   clips folder** on the Welcome card: `Win64\Mods\SessionAnimLoader\clips`.

---

## Uninstall

1. Close the game and Session Studio.
2. Double-click **UNINSTALL.cmd**: the one in the package, or the copy the installer left in
   `Win64\SessionAnimLoader_uninstall\`.
3. Read the list of what will happen, then answer **y**.

The installer writes down everything it does (`Win64\SessionAnimLoader_install.json`). The uninstaller undoes that,
back to how things were **before the first installation**, even after several updates:

| What | What the uninstaller does |
|---|---|
| Files the installer added (mod, editor, desktop shortcut) | deletes them |
| Files the installer replaced | puts the earlier version back |
| Files that were already there, or that you changed afterwards | keeps them and tells you |
| UE4SS | removed only if the installer put it there, it has not changed **and no other mod is in `Mods`**; otherwise kept |
| Your clips, recorded moves and captures | **never deleted**: moved to `Documents\SessionStudio\clips-after-uninstall_<date>\` (they stay in place if the folder was there before the first installation) |
| The editor's clip backups (`Documents\SessionStudio\backups\`) | not touched |
| Folders the installer created | removed when empty |

To use your clips again after reinstalling, copy the `.sanim` files, `_skeleton.json` and the `_figures` folder from
that folder into the new `clips` folder.

If there is no record (you copied the files by hand), the uninstaller touches nothing: use the steps below.

### Uninstall by hand

**Back up your clips first** if you want to keep them: they are inside the mod's folder,
`Win64\Mods\SessionAnimLoader\clips\`.

1. Close the game and Session Studio.
2. Delete `Win64\Mods\SessionAnimLoaderNative\` and `Win64\Mods\SessionAnimLoader\`.
3. If you wish, delete `Win64\SessionAnimLoader.native.log`, `Win64\SessionAnimLoader_backups\`,
   `Win64\SessionAnimLoader_uninstall\` and `Win64\SessionAnimLoader_install.json`.
4. Delete the editor's folder and its desktop shortcut. The editor's clip backups are in
   `Documents\SessionStudio\backups\`.

**UE4SS**: keep it if you use other mods (SessionOpenMP, ExpandedTricks and others need it). To remove it too, delete
`dwmapi.dll`, `UE4SS.dll`, `UE4SS-settings.ini` and `UE4SS.log` from `Win64\`. Delete the `Mods` folder only if nothing
else is left in it.

**To switch the mod off without removing it**: press **Ctrl+F3** in game (until the game restarts), or delete the
`enabled.txt` file in both mod folders. Running the installer again switches it back on.

---

## Troubleshooting

### The installer stops

| Message | What it means and what to do |
|---|---|
| `files\install.ps1 is missing` or `the package is incomplete` | extract the whole zip into a normal folder, then run INSTALLER.cmd from there. |
| `the game is running` | close the game. If it is closed, check the Task Manager for `SessionGame-Win64-Shipping.exe`. |
| `SessionGame-Win64-Shipping.exe was not found` | choose the folder of the game itself (the one named `Session`). |
| `a newer layout of UE4SS is installed` | you have a `Win64\ue4ss\` folder: another UE4SS version. Session Anim Loader needs 3.0.1. Using it means going back to 3.0.1, which may break mods that need the newer one: your choice. |
| `UE4SS 3.0.1 could not be confirmed` | your `UE4SS.dll` is not the official 3.0.1 file and its log does not say 3.0.1. If you do not know what it is, do not force it: a wrong version can make the game crash at start. |
| `UE4SS.dll is not there but dwmapi.dll is` (or `xinput1_3.dll`) | the remains of an old or disabled UE4SS, or another mod loader. If you know it is left over from UE4SS, delete that file and run the installer again. |
| the editor could not be copied | choose a folder you can write to (not Program Files), with Session Studio closed. |

### In game

| What you see | What to do |
|---|---|
| The game closes or crashes at start | rename `Win64\dwmapi.dll` (this switches every UE4SS mod off). If the game then starts, check that your UE4SS is 3.0.1 (first lines of `UE4SS.log`). |
| No `UE4SS.log` in `Win64` | UE4SS does not load: check that `dwmapi.dll` and `UE4SS.dll` are both in `Win64`. |
| No `SessionAnimLoader.native.log` | the mod does not load. In `UE4SS.log`, look for `Starting C++ mod 'SessionAnimLoaderNative'`. Check the two `enabled.txt` files and install the Visual C++ Redistributable (x64). |
| The log says `installation: refused` | read the rest of the line. After a **game update**, the mod may refuse to hook in, on purpose, without breaking anything: wait for a new version of the mod. If the line is about ExpandedTricksNative, it was enabled but did not start. |
| Nothing changes in game | press Ctrl+F3 (the mod may be off). Is the clip switched on in the editor? Was it made for this stance and foot? |
| Another player does not see your clips | both need SessionOpenMP, the same mod version, and the other player needs a copy of your clip file in `clips\others\`. Versions before v0.24 cannot see later ones. |

### In the editor

| What you see | What to do |
|---|---|
| An error about `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` or `MSVCP140.dll` | install the [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vc14/vc_redist.x64.exe). |
| `Direct3D 11 is unavailable` | update your graphics driver. |
| Red dot in the Welcome card | follow the sentence under that line. The card is in the **...** menu, **Welcome and setup check**. |
| The editor shows no skeleton or no move | start the game once with the mod, skate, and do the move once. |

---

## Reporting a problem

Say your mod version, whether you use ExpandedTricks or SessionOpenMP, and what you did. Attach a screenshot, and from
the `Win64` folder: `UE4SS.log` and `SessionAnimLoader.native.log` (copy them **before** starting the game again: they
are rewritten at each launch). These logs contain folder paths from your PC: look at them before sharing.

---

## What works and what does not

- **Works:** flip tricks (per stance and foot), manual and nose manual, on foot, pushing, crouch, landing, dismount,
  pose captures.
- **Experimental:** riding poses, walking and running (known problems), multiplayer through SessionOpenMP (tried with
  two players on an earlier version).
- **Not supported:** grinds and grabs. Powerslide has not been checked.

The editor shows a mannequin, not your skater, and the game adds its own physics and blending: always check your clip
in game.
