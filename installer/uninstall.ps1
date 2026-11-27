# Session Studio and Session Anim Loader: uninstaller (run by UNINSTALL.cmd, from a temporary copy).
#
# It undoes what the installer wrote down in SessionAnimLoader_install.json, and nothing else:
#   - a file the installer added is deleted, only if it is still the file the installer wrote;
#   - a file the installer replaced gets its earlier version back;
#   - a folder the installer created is removed only when it is empty;
#   - UE4SS is removed only if the installer put it there, it has not changed, and no other mod is in Mods;
#   - your clips and recordings are never deleted: they are moved to Documents\SessionStudio;
#   - the editor's clip backups (Documents\SessionStudio\backups) are not touched.
# Without that record (an installation made by hand), it touches nothing.
#
# It shows the whole list and asks before doing anything.
#
# Options (for testing): -GameDir <folder>, -KeepDir <folder for the clips>, -Yes (no question; needs -GameDir)
param(
    [string]$GameDir = "",
    [string]$KeepDir = "",
    [string]$From = "",
    [switch]$Yes
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

function Exit-Uninstall([int]$code) {
    if (-not $Yes) { Write-Host ""; [void](Read-Host "Press Enter to close") }
    exit $code
}
function Stop-Uninstall([string]$m) {
    Write-Host ""
    Write-Host "STOPPED: $m" -ForegroundColor Red
    Exit-Uninstall 1
}
function Test-NoItem([string]$d) { return @(Get-ChildItem -LiteralPath $d -Force).Count -eq 0 }

try {
    Say "Session Studio and Session Anim Loader: uninstaller"

    # ---------------------------------------------------------------- the game folder
    $win = $null
    if ($GameDir) {
        $win = Resolve-Win64 $GameDir
        if (-not $win) { Stop-Uninstall "$GameExe was not found in or under: $GameDir" }
    } else {
        if ($Yes) { Stop-Uninstall "-Yes needs -GameDir." }
        if ($From) {
            $parent = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($From).TrimEnd('\'))
            if ($parent -and (Test-File "$parent\$RecordName")) { $win = Resolve-Win64 $parent }
        }
        if (-not $win) {
            $found = Find-SteamGame
            if ($found -and (Read-YesNo "  Game found: $found. Uninstall from there?" $true)) { $win = $found }
        }
        while (-not $win) {
            $pick = Select-Folder "Choose the Session: Skate Sim folder (or its SessionGame\Binaries\Win64 folder)" ""
            if (-not $pick) { Stop-Uninstall "no folder chosen; nothing was changed." }
            $win = Resolve-Win64 $pick
            if (-not $win) { Warn "$GameExe is not in that folder, or under it. Try again (Cancel to quit)." }
        }
    }
    Good "game: $win"
    if (Get-Process -Name "SessionGame-Win64-Shipping" -ErrorAction SilentlyContinue) { Stop-Uninstall "the game is running. Close it first. Nothing was changed." }

    try { $rec = Read-Record $win }
    catch { Stop-Uninstall "the installation record cannot be read ($($_.Exception.Message)). Nothing was changed." }
    if (-not $rec) { Stop-Uninstall "there is no installation record ($RecordName) in the game folder: the mod was installed by hand or by an older package, or it is already removed. Nothing was changed. To remove it by hand, see INSTALL.md, 'Uninstall by hand'." }
    foreach ($p in @(Get-Process -Name "SessionStudio" -ErrorAction SilentlyContinue)) {
        foreach ($d in $rec.editorDirs) { if ($p.Path -eq "$d\SessionStudio.exe") { Stop-Uninstall "Session Studio is open from $d. Close it first. Nothing was changed." } }
    }

    # ---------------------------------------------------------------- the plan (nothing is changed yet)
    $delete = @(); $restore = @(); $leave = @(); $retained = @{}
    $ueKeys = @($rec.files.Keys | Where-Object { $rec.files[$_].kind -eq "ue4ss" })
    $ueWhy = $null
    if ($ueKeys.Count -gt 0) {
        $others = @()
        if (Test-Dir "$win\Mods") { $others = @(Get-ChildItem -LiteralPath "$win\Mods" -Force | Where-Object { "SessionAnimLoader", "SessionAnimLoaderNative", "mods.txt" -notcontains $_.Name } | ForEach-Object { $_.Name }) }
        $changed = @($ueKeys | Where-Object { $f = Get-Full $win $_; (Test-File $f) -and (Get-Sha $f) -ne $rec.files[$_].sha })
        if ($others.Count -gt 0) { $ueWhy = "other mods are in Mods and need it: $($others -join ', ')" }
        elseif ($changed.Count -gt 0) { $ueWhy = "changed since the installation: $($changed -join ', ')" }
    }

    foreach ($key in @($rec.files.Keys | Sort-Object)) {
        $e = $rec.files[$key]
        $full = Get-Full $win $key
        if ($e.kind -eq "ue4ss" -and $ueWhy) { $retained[$key] = $e; continue }
        $backup = if ($e.backup) { Get-Full $win $e.backup } else { $null }
        if ($e.existed -and -not $backup) {
            if (Test-File $full) { $leave += "$full (it was there before the first installation)" }
            continue
        }
        if ($e.kind -eq "editorIni") { if (Test-File $full) { $delete += $full }; continue }
        if ((Test-File $full) -and (Get-Sha $full) -ne $e.sha) {
            $leave += "$full (changed since the installation)"
            $retained[$key] = $e
            continue
        }
        if ($backup) {
            if (Test-File $backup) { $restore += ,@($backup, $full) }
            else { $leave += "$full (its earlier version is missing: $backup)"; $retained[$key] = $e }
        } elseif (Test-File $full) { $delete += $full }
    }
    foreach ($d in $rec.editorDirs) {   # written by the editor's --selftest
        if ($delete -contains "$d\SessionStudio.exe" -and (Test-File "$d\selftest.txt")) { $delete += "$d\selftest.txt" }
    }

    $modGone = $delete -contains "$win\Mods\SessionAnimLoaderNative\dlls\main.dll"
    if ($modGone -and -not $rec.logs["SessionAnimLoader.native.log"] -and (Test-File "$win\SessionAnimLoader.native.log")) { $delete += "$win\SessionAnimLoader.native.log" }
    if ($ueKeys.Count -gt 0 -and -not $ueWhy -and -not $rec.logs["UE4SS.log"] -and (Test-File "$win\UE4SS.log")) { $delete += "$win\UE4SS.log" }

    # the player's own files: the clips folder, with the recordings and captures
    $clipsKey = "Mods\SessionAnimLoader\clips"
    $clips = "$win\$clipsKey"
    $clipFiles = @()
    if (Test-Dir $clips) { $clipFiles = @(Get-ChildItem -LiteralPath $clips -Recurse -File -Force) }
    $moveClips = ($rec.dirs -contains $clipsKey) -and $clipFiles.Count -gt 0
    if (-not $KeepDir) { $KeepDir = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "SessionStudio" }
    $kept = Join-Path $KeepDir ("clips-after-uninstall_" + (Get-Date -Format "yyyy-MM-dd_HH-mm-ss"))
    if ($clipFiles.Count -gt 0 -and -not $moveClips) { $leave += "$clips (your clips folder was there before the first installation)" }

    $link = $null
    if ($rec.shortcut -and (Test-File $rec.shortcut.path)) {
        $target = (New-Object -ComObject WScript.Shell).CreateShortcut($rec.shortcut.path).TargetPath
        if ($target -eq $rec.shortcut.target -and $delete -contains $rec.shortcut.target) { $link = $rec.shortcut.path }
    }

    Step "What will happen"
    if ($moveClips) { Say "  MOVE your clips and recordings ($($clipFiles.Count) files) to: $kept" }
    foreach ($f in $delete) { Say "  delete   $f" }
    foreach ($r in $restore) { Say "  restore  $($r[1])  (the version from before)" }
    if ($link) { Say "  delete   $link  (desktop shortcut)" }
    if ($ueKeys.Count -gt 0 -and $ueWhy) { Warn "UE4SS is KEPT: $ueWhy" }
    elseif ($ueKeys.Count -eq 0) { Say "  UE4SS is not touched: this installer did not put it there." }
    foreach ($l in $leave) { Warn "kept: $l" }
    Say "  Folders the installer created are removed when they are empty. The editor's clip backups are not touched."
    if (-not $Yes -and -not (Read-YesNo "  Uninstall now?" $false)) { Stop-Uninstall "cancelled; nothing was changed." }

    # ---------------------------------------------------------------- do it: the player's files first
    if ($moveClips) {
        foreach ($f in $clipFiles) {
            $to = $kept + $f.FullName.Substring($clips.Length)
            New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($to)) | Out-Null
            Copy-Item -LiteralPath $f.FullName -Destination $to
            if ((Get-Sha $f.FullName) -ne (Get-Sha $to)) { Stop-Uninstall "a clip could not be copied safely ($($f.FullName)). Nothing was deleted." }
        }
        # every copy is checked: only now are the originals removed
        foreach ($f in $clipFiles) { Remove-Item -LiteralPath $f.FullName -Force }
        foreach ($d in @(Get-ChildItem -LiteralPath $clips -Recurse -Directory -Force | Sort-Object { $_.FullName.Length } -Descending)) {
            if (Test-NoItem $d.FullName) { [System.IO.Directory]::Delete($d.FullName) }
        }
    }
    foreach ($r in $restore) {
        Copy-Item -LiteralPath $r[0] -Destination $r[1] -Force
        if ((Get-Sha $r[0]) -ne (Get-Sha $r[1])) { Stop-Uninstall "the earlier version of $($r[1]) could not be restored." }
    }
    foreach ($f in $delete) { Remove-Item -LiteralPath $f -Force }
    if ($link) { Remove-Item -LiteralPath $link -Force }

    # the installer's backup copies, except those still needed by a file that was kept
    $needed = @($retained.Values | ForEach-Object { $_.backup } | Where-Object { $_ })
    $backupsLeft = @{}
    foreach ($key in @($rec.backups.Keys)) {
        $f = Get-Full $win $key
        if (-not (Test-File $f)) { continue }
        if ($needed -notcontains $key -and (Get-Sha $f) -eq $rec.backups[$key]) { Remove-Item -LiteralPath $f -Force }
        else { $backupsLeft[$key] = $rec.backups[$key] }
    }

    # something of ours stays (UE4SS in use, a file you changed): the record keeps it, so the uninstaller can be run
    # again later (from the package); else the record goes
    if ($retained.Count -gt 0) {
        $rec.files = $retained
        $rec.backups = $backupsLeft
        $rec.editorDirs = @($rec.editorDirs | Where-Object { Test-File "$_\SessionStudio.exe" })
        $rec.shortcut = $null
        Save-Record $win $rec
    } else {
        Remove-Item -LiteralPath "$win\$RecordName" -Force
    }

    # deepest first; a folder that still holds something stays ([IO.Directory]::Delete never removes contents)
    $left = @()
    foreach ($key in @($rec.dirs | Sort-Object { (Get-Full $win $_).Length } -Descending)) {
        $d = Get-Full $win $key
        if (-not (Test-Dir $d)) { continue }
        if (Test-NoItem $d) { [System.IO.Directory]::Delete($d) } else { $left += $d }
    }
    if ($retained.Count -gt 0) { $rec.dirs = @($left | ForEach-Object { Get-Key $win $_ }); Save-Record $win $rec }

    Step "Done."
    if ($moveClips) { Good "your clips and recordings are in: $kept" }
    foreach ($d in $left) { Say "  folder kept because it is not empty: $d" }
    if ($retained.Count -gt 0) { Say "  Some files were kept (see above). Their record stays: you can run UNINSTALL.cmd from the package again later." }
    if ($PSScriptRoot -like "*\SessionStudio-uninstall-*") { Remove-Item -LiteralPath "$PSScriptRoot\uninstall.ps1", "$PSScriptRoot\common.ps1" -ErrorAction SilentlyContinue }
    Exit-Uninstall 0
} catch {
    Write-Host ""
    Write-Host "STOPPED by an error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Part of the uninstall may be done. Run it again, or send this message with a bug report."
    Exit-Uninstall 1
}
