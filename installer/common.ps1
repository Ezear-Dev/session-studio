# Functions shared by install.ps1 and uninstall.ps1.

$GameExe = "SessionGame-Win64-Shipping.exe"
$RecordName = "SessionAnimLoader_install.json"   # in Win64, outside Mods: what the installer did, for the uninstaller

function Say([string]$m) { Write-Host $m }
function Step([string]$m) { Write-Host ""; Write-Host $m -ForegroundColor Cyan }
function Good([string]$m) { Write-Host "  $m" -ForegroundColor Green }
function Warn([string]$m) { Write-Host "  ! $m" -ForegroundColor Yellow }
function Test-File([string]$p) { return (Test-Path -LiteralPath $p -PathType Leaf) }
function Test-Dir([string]$p) { return (Test-Path -LiteralPath $p -PathType Container) }
function Get-Sha([string]$p) { return (Get-FileHash -LiteralPath $p).Hash }

function Read-YesNo([string]$question, [bool]$default) {
    if ($Yes) { return $default }
    $hint = if ($default) { "[Y/n]" } else { "[y/N]" }
    $a = (Read-Host "$question $hint").Trim().ToLowerInvariant()
    if ($a -eq "") { return $default }
    return ($a.StartsWith("y") -or $a.StartsWith("o"))
}

# the Windows folder picker; $null when cancelled
function Select-Folder([string]$title, [string]$start) {
    Add-Type -AssemblyName System.Windows.Forms
    $owner = New-Object System.Windows.Forms.Form
    $owner.TopMost = $true
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = $title
    $dlg.ShowNewFolderButton = $true
    if ($start -and (Test-Dir $start)) { $dlg.SelectedPath = $start }
    $r = $dlg.ShowDialog($owner)
    $owner.Dispose()
    if ($r -ne [System.Windows.Forms.DialogResult]::OK) { return $null }
    return $dlg.SelectedPath
}

# the Win64 folder that holds the game's executable, from the folder given or one of its usual parents
function Resolve-Win64([string]$p) {
    if (-not $p) { return $null }
    $p = $p.Trim().Trim('"').TrimEnd('\')
    foreach ($sub in "", "\SessionGame\Binaries\Win64", "\Binaries\Win64", "\Win64", "\Session\SessionGame\Binaries\Win64") {
        if (Test-File "$p$sub\$GameExe") { return (Get-Item -LiteralPath "$p$sub").FullName }
    }
    return $null
}

function Find-SteamGame {
    $libs = @()
    $steam = $null
    try { $steam = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -Name SteamPath).SteamPath } catch {}
    if ($steam) {
        $steam = $steam -replace '/', '\'
        $libs += $steam
        $vdf = "$steam\steamapps\libraryfolders.vdf"
        if (Test-File $vdf) {
            foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw -Encoding UTF8), '"path"\s+"([^"]+)"')) {
                $libs += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }
    $libs += "C:\Program Files (x86)\Steam"
    foreach ($l in $libs) {
        $w = Resolve-Win64 "$l\steamapps\common\Session"
        if ($w) { return $w }
    }
    return $null
}

# ------------------------------------------------------------------ the install record
# files      path -> existed (before the FIRST install), sha (what the installer wrote), backup (the earlier file), kind
# backups    path -> sha of every backup copy the installer made
# dirs       the folders the installer created
# logs       name -> existed before the first install
# editorDirs the editor's folders; shortcut: the desktop shortcut
# Paths inside the game are relative to Win64; others are full paths.
function New-Record { return @{ files = @{}; backups = @{}; dirs = @(); logs = @{}; editorDirs = @(); shortcut = $null } }

function ConvertTo-Table($o) { $t = @{}; if ($o) { foreach ($p in $o.PSObject.Properties) { $t[$p.Name] = $p.Value } }; return $t }

function Read-Record([string]$win) {   # $null when there is none; throws when it cannot be read
    $path = "$win\$RecordName"
    if (-not (Test-File $path)) { return $null }
    $j = [System.IO.File]::ReadAllText($path) | ConvertFrom-Json
    if ($j.format -ne 1) { throw "$RecordName has an unknown format" }
    $files = @{}
    foreach ($p in $j.files.PSObject.Properties) { $files[$p.Name] = ConvertTo-Table $p.Value }
    return @{ files = $files; backups = (ConvertTo-Table $j.backups); dirs = @($j.dirs | Where-Object { $_ })
              logs = (ConvertTo-Table $j.logs); editorDirs = @($j.editorDirs | Where-Object { $_ }); shortcut = $j.shortcut }
}

function Save-Record([string]$win, $rec) {
    $o = [ordered]@{ format = 1; files = $rec.files; backups = $rec.backups; dirs = @($rec.dirs); logs = $rec.logs
                     editorDirs = @($rec.editorDirs); shortcut = $rec.shortcut }
    $tmp = "$win\$RecordName.new"
    [System.IO.File]::WriteAllText($tmp, ($o | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding($false)))
    Move-Item -LiteralPath $tmp -Destination "$win\$RecordName" -Force
}

function Get-Key([string]$win, [string]$full) {
    if ($full.StartsWith("$win\", [System.StringComparison]::OrdinalIgnoreCase)) { return $full.Substring($win.Length + 1) }
    return $full
}
function Get-Full([string]$win, [string]$key) {
    if ([System.IO.Path]::IsPathRooted($key)) { return $key }
    return "$win\$key"
}
