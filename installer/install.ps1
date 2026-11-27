# Session Studio and Session Anim Loader: installer (run by INSTALLER.cmd; it sits in the package's "files" folder).
#
# What it does, in order:
#   1. finds the game's Win64 folder (Steam), and asks you to confirm it or to choose it
#   2. installs UE4SS 3.0.1 ONLY if no UE4SS is there. An existing UE4SS is never modified.
#   3. backs up an earlier version of the mod, then copies the new one. Your clips folder is never emptied or replaced.
#   4. asks where to put the Session Studio editor, and whether to create a desktop shortcut
# Everything it does is written down in SessionAnimLoader_install.json (in the game's Win64 folder): the uninstaller
# undoes exactly that.
#
# What it will not do: download anything, contact any server, change the game's own files, or delete your files.
#
# Options (for testing or unattended installs):
#   -GameDir <folder>     the game folder (or its Win64 folder): no search, no question
#   -EditorDir <folder>   where to put the editor: no folder window
#   -SkipEditor           install the game mod only
#   -Yes                  ask nothing (the editor is installed only with -EditorDir, or updated where it was; no shortcut)
param(
    [string]$GameDir = "",
    [string]$EditorDir = "",
    [switch]$SkipEditor,
    [switch]$Yes
)
$ErrorActionPreference = "Stop"

$Files = $PSScriptRoot
if (-not (Test-Path -LiteralPath "$Files\common.ps1")) { Write-Host "STOPPED: the package is incomplete (files\common.ps1 is missing). Extract the whole zip again."; exit 1 }
. "$Files\common.ps1"

function Stop-Install([string]$m) {
    Write-Host ""
    Write-Host "STOPPED: $m" -ForegroundColor Red
    exit 1
}

# empty, or holding only what Session Studio itself leaves behind: its clip backups are in Documents\SessionStudio\backups,
# and its settings may stay after the program is removed
function Test-Empty([string]$p) { return (-not (Test-Dir $p)) -or (@(Get-ChildItem -LiteralPath $p -Force | Where-Object { $_.Name -ne "backups" -and $_.Name -ne "SessionStudio.ini" }).Count -eq 0) }

# the editor always gets its own "SessionStudio" folder inside the folder chosen; a folder that already holds the
# editor is updated in place
function Get-EditorFolder([string]$pick) {
    $p = [System.IO.Path]::GetFullPath($pick).TrimEnd('\')
    if (Test-File "$p\SessionStudio.exe") { return $p }
    return (Join-Path $p "SessionStudio")
}
# a folder with other things in it (a copy of the source code, for example): the editor is never mixed into it
function Test-ForeignFolder([string]$d) { return (-not (Test-File "$d\SessionStudio.exe")) -and (-not (Test-Empty $d)) }

# creates a folder and writes down every level that did not exist
function New-Folder([string]$d) {
    $missing = @()
    $p = $d
    while ($p -and -not (Test-Dir $p)) { $missing += $p; $p = [System.IO.Path]::GetDirectoryName($p) }
    if ($missing.Count -eq 0) { return }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    foreach ($m in $missing) { $k = Get-Key $win $m; if ($rec.dirs -notcontains $k) { $rec.dirs += $k } }
}

function Copy-Checked([string]$from, [string]$to) {
    Copy-Item -LiteralPath $from -Destination $to -Force
    if ((Get-Sha $from) -ne (Get-Sha $to)) { Stop-Install "the copy of $to is not identical to the package file." }
}

# copies one file of the package: a different file already there is backed up first, and the record is written before
# the copy. What was there before the FIRST installation is never forgotten by an update.
function Install-File([string]$from, [string]$to, [string]$kind) {
    $key = Get-Key $win $to
    $new = Get-Sha $from
    $had = Test-File $to
    $backupKey = $null
    if ($had -and (Get-Sha $to) -ne $new) {
        # backups sit outside Mods, so UE4SS never loads one
        $inside = if ([System.IO.Path]::IsPathRooted($key)) { "editor\" + $to.Substring($script:dest.Length + 1) } else { $key }
        $copy = "$win\SessionAnimLoader_backups\$stamp\$inside"
        New-Folder ([System.IO.Path]::GetDirectoryName($copy))
        Copy-Checked $to $copy
        $backupKey = Get-Key $win $copy
        $rec.backups[$backupKey] = Get-Sha $copy
        $script:backedUp = "$win\SessionAnimLoader_backups\$stamp"
    }
    New-Folder ([System.IO.Path]::GetDirectoryName($to))
    if ($rec.files.ContainsKey($key)) { $rec.files[$key].sha = $new }
    else { $rec.files[$key] = @{ existed = $had; sha = $new; backup = $backupKey; kind = $kind } }
    Save-Record $win $rec
    Copy-Checked $from $to
}

try {
    Say "Session Studio and Session Anim Loader: installer"

    # ---------------------------------------------------------------- the package
    foreach ($need in "Game\Mods\SessionAnimLoaderNative\dlls\main.dll", "Game\Mods\SessionAnimLoaderNative\enabled.txt",
        "Game\Mods\SessionAnimLoader\Scripts\main.lua", "Game\Mods\SessionAnimLoader\enabled.txt",
        "UE4SS\UE4SS.dll", "UE4SS\dwmapi.dll", "UE4SS\UE4SS-settings.ini", "UE4SS\Mods\mods.txt",
        "Editor\SessionStudio.exe", "uninstall.ps1", "..\UNINSTALL.cmd") {
        if (-not (Test-File "$Files\$need")) { Stop-Install "the package is incomplete (files\$need is missing). Extract the whole zip again." }
    }

    # ---------------------------------------------------------------- 1. the game folder
    Step "1. Game folder"
    $win = $null
    if ($GameDir) {
        $win = Resolve-Win64 $GameDir
        if (-not $win) { Stop-Install "$GameExe was not found in or under: $GameDir" }
    } else {
        $found = Find-SteamGame
        if ($found) {
            Say "  found: $found"
            if (Read-YesNo "  Install into this folder?" $true) { $win = $found }
        } else { Warn "the game was not found automatically." }
        while (-not $win) {
            if ($Yes) { Stop-Install "the game folder was not found; give it with -GameDir." }
            Say "  A window opens: choose the game's folder (the one named Session, or its Win64 folder)."
            $pick = Select-Folder "Choose the Session: Skate Sim folder (or its SessionGame\Binaries\Win64 folder)" $found
            if (-not $pick) { Stop-Install "no folder chosen; nothing was changed." }
            $win = Resolve-Win64 $pick
            if (-not $win) { Warn "$GameExe is not in that folder, or under it. Try again (Cancel to quit)." }
        }
    }
    Good "game: $win"
    if ($win -notlike "*\steamapps\*") { Warn "this does not look like a Steam install. The mod was only tested with the Steam version." }
    if (Get-Process -Name "SessionGame-Win64-Shipping" -ErrorAction SilentlyContinue) { Stop-Install "the game is running. Close it, then run the installer again." }

    try { $rec = Read-Record $win }
    catch { Stop-Install "the record of the earlier installation cannot be read ($($_.Exception.Message)). Nothing was changed." }
    if (-not $rec) {
        $rec = New-Record
        foreach ($log in "UE4SS.log", "SessionAnimLoader.native.log") { $rec.logs[$log] = Test-File "$win\$log" }
    }
    $stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
    $script:backedUp = $null
    $script:dest = $null

    # ---------------------------------------------------------------- 2. UE4SS
    Step "2. UE4SS (the mod loader)"
    if (Test-File "$win\ue4ss\UE4SS.dll") {
        Stop-Install "a newer layout of UE4SS is installed (a 'ue4ss' sub-folder). Session Anim Loader needs UE4SS 3.0.1. Nothing was changed. See INSTALL.md, 'Troubleshooting'."
    }
    if (Test-File "$win\UE4SS.dll") {
        $same = (Get-Sha "$win\UE4SS.dll") -eq (Get-Sha "$Files\UE4SS\UE4SS.dll")
        if ($same) {
            Good "UE4SS 3.0.1 is already installed: it is left as it is."
        } else {
            $ver = ""
            if (Test-File "$win\UE4SS.log") {
                $m = Get-Content -LiteralPath "$win\UE4SS.log" -TotalCount 20 | Select-String -Pattern "UE4SS - v(\d+\.\d+\.\d+)" | Select-Object -First 1
                if ($m) { $ver = $m.Matches[0].Groups[1].Value }
            }
            if ($ver -eq "3.0.1") {
                Good "UE4SS is already installed (its last log says 3.0.1): it is left as it is."
            } else {
                $what = if ($ver) { "its last log says version $ver" } else { "its version could not be read" }
                Warn "UE4SS is already installed and is not the file in this package ($what)."
                Warn "Session Anim Loader needs UE4SS 3.0.1. With another version the mod may not load, and the game can even crash at start."
                Warn "Your UE4SS is not touched. Only continue if you know it is 3.0.1."
                if (-not (Read-YesNo "  Install the mod anyway?" $false)) { Stop-Install "UE4SS 3.0.1 could not be confirmed; nothing was changed. See INSTALL.md, 'Troubleshooting'." }
            }
        }
        if (-not (Test-File "$win\dwmapi.dll")) { Warn "dwmapi.dll is missing, so UE4SS may be switched off. If the mod does not load, see INSTALL.md, 'Troubleshooting'." }
    } else {
        foreach ($other in "dwmapi.dll", "xinput1_3.dll") {
            if (Test-File "$win\$other") { Stop-Install "UE4SS.dll is not there but $other is: another mod loader, or the remains of an old one. Nothing was changed. See INSTALL.md, 'Troubleshooting'." }
        }
        if (Test-File "$win\UE4SS-settings.ini") { Warn "UE4SS-settings.ini already exists: it is kept. If UE4SS does not load, see INSTALL.md." }
        else { Install-File "$Files\UE4SS\UE4SS-settings.ini" "$win\UE4SS-settings.ini" "ue4ss" }
        if (-not (Test-File "$win\Mods\mods.txt")) { Install-File "$Files\UE4SS\Mods\mods.txt" "$win\Mods\mods.txt" "ue4ss" }
        Install-File "$Files\UE4SS\UE4SS.dll" "$win\UE4SS.dll" "ue4ss"
        Install-File "$Files\UE4SS\dwmapi.dll" "$win\dwmapi.dll" "ue4ss"
        Good "UE4SS 3.0.1 installed."
    }

    # ---------------------------------------------------------------- 3. the mod
    Step "3. Session Anim Loader (the game mod)"
    foreach ($old in "AnimStudio", "AnimStudioNative") {
        if (Test-File "$win\Mods\$old\enabled.txt") { Warn "an old test version is still enabled: delete Mods\$old\enabled.txt, or the two mods will run together." }
    }
    foreach ($f in "Mods\SessionAnimLoaderNative\dlls\main.dll", "Mods\SessionAnimLoaderNative\enabled.txt",
                   "Mods\SessionAnimLoader\Scripts\main.lua", "Mods\SessionAnimLoader\enabled.txt") {
        Install-File "$Files\Game\$f" "$win\$f" "mod"
    }
    $clips = "$win\Mods\SessionAnimLoader\clips"
    New-Folder "$clips\others"
    # a copy of the uninstaller stays with the game, for when the package is gone
    Install-File "$Files\..\UNINSTALL.cmd" "$win\SessionAnimLoader_uninstall\UNINSTALL.cmd" "uninstaller"
    foreach ($f in "uninstall.ps1", "common.ps1") { Install-File "$Files\$f" "$win\SessionAnimLoader_uninstall\$f" "uninstaller" }
    Save-Record $win $rec
    if ($script:backedUp) { Good "earlier version backed up in: $($script:backedUp)" }
    Good "mod installed. Your clips folder: $clips"

    # ---------------------------------------------------------------- 4. the editor
    Step "4. Session Studio (the editor)"
    $dest = $null
    $pick = $EditorDir
    $known = @($rec.editorDirs | Where-Object { Test-File "$_\SessionStudio.exe" }) | Select-Object -Last 1
    if (-not $SkipEditor -and -not $EditorDir -and $known) {
        if (Read-YesNo "  Session Studio is installed in $known. Update it there?" $true) { $pick = $known }
    }
    $ask = (-not $pick) -and (-not $Yes)
    while (-not $SkipEditor) {
        if ($ask) {
            Say "  A window opens: choose where to put the editor (Cancel = do not install it)."
            Say "  A 'SessionStudio' folder is created inside the folder you choose. Pick a place you can write to, not Program Files."
            $pick = Select-Folder "Where should Session Studio (the editor) be installed? A 'SessionStudio' folder is created inside the folder you choose. Cancel = skip." ([Environment]::GetFolderPath("MyDocuments"))
        }
        if (-not $pick) { break }
        $d = Get-EditorFolder $pick
        if (Test-ForeignFolder $d) {
            Warn "$d already holds other files and no Session Studio: the editor is not put there."
            if (-not $ask) { Stop-Install "choose another folder for the editor. (The game mod is already installed.)" }
            continue
        }
        $dest = $d
        break
    }
    if (-not $dest) {
        Warn "editor not installed. You can run the installer again later."
    } else {
        $script:dest = $dest
        Say "  installing the editor in: $dest"
        foreach ($p in @(Get-Process -Name "SessionStudio" -ErrorAction SilentlyContinue)) {
            if ($p.Path -eq "$dest\SessionStudio.exe") { Stop-Install "Session Studio is open from $dest. Close it, then run the installer again. (The game mod is already installed.)" }
        }
        try {
            New-Folder $dest
            if ($rec.editorDirs -notcontains $dest) { $rec.editorDirs += $dest }
            $src = (Get-Item -LiteralPath "$Files\Editor").FullName
            foreach ($f in Get-ChildItem -LiteralPath $src -Recurse -File) { Install-File $f.FullName ($dest + $f.FullName.Substring($src.Length)) "editor" }
            # the editor's settings: written on a first install (so it finds the clips even outside a standard Steam
            # folder), then changed by the editor itself
            $ini = "$dest\SessionStudio.ini"
            if (-not $rec.files.ContainsKey($ini)) { $rec.files[$ini] = @{ existed = (Test-File $ini); sha = $null; backup = $null; kind = "editorIni" } }
            Save-Record $win $rec
            if (-not (Test-File $ini)) { [System.IO.File]::WriteAllText($ini, "clipsDir=$clips`n", (New-Object System.Text.UTF8Encoding($false))) }
        } catch {
            Stop-Install "the editor could not be copied to $dest ($($_.Exception.Message)). Choose a folder you can write to and run the installer again. (The game mod is already installed.)"
        }
        Good "editor installed: $dest\SessionStudio.exe"
        if (-not $Yes -and (Read-YesNo "  Create a desktop shortcut?" $true)) {
            try {
                $link = Join-Path ([Environment]::GetFolderPath("Desktop")) "Session Studio.lnk"
                $sc = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
                $sc.TargetPath = "$dest\SessionStudio.exe"
                $sc.WorkingDirectory = $dest
                $sc.Save()
                $rec.shortcut = @{ path = $link; target = "$dest\SessionStudio.exe" }
                Save-Record $win $rec
                Good "shortcut created on the desktop."
            } catch { Warn "the shortcut could not be created: $($_.Exception.Message)" }
        }
    }

    # ---------------------------------------------------------------- done
    Step "Done."
    if (-not (Test-File "$env:SystemRoot\System32\VCRUNTIME140_1.dll") -or -not (Test-File "$env:SystemRoot\System32\MSVCP140.dll")) {
        Warn "the Microsoft Visual C++ runtime (x64) seems to be missing. Install it from https://aka.ms/vc14/vc_redist.x64.exe"
    }
    Say "  Next: start the game, put the skater on the board and wait about 10 seconds."
    Say "  Then open Session Studio: the Welcome card checks your setup. See INSTALL.md and docs\Getting-Started."
    Say "  To remove everything later: UNINSTALL.cmd (in the package, or in $win\SessionAnimLoader_uninstall)."
    exit 0
} catch {
    Write-Host ""
    Write-Host "STOPPED by an error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Part of the installation may be done. Run the installer again, or send this message with a bug report."
    exit 1
}
