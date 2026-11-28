# Builds the release package from an explicit list of files (never a whole folder of the working copy):
#   release\out\SessionStudio-v0.N.zip            the player package (installer, game mod, UE4SS, editor, guides)
#   release\out\SessionStudio-v0.N-source.zip     the source code of that version (GPL-3.0), to publish beside it
#   release\out\SHA256SUMS.txt
# Run build.cmd and test.cmd first. Needs git and node on the PATH, and the official UE4SS_v3.0.1.zip in
# release\cache (it is never downloaded by this script):
#   https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/v3.0.1
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1
# -Dll / -Exe: package other binaries than the ones in the dist folders (for example when an open editor locks
# dist\SessionStudio.exe). The exe's version is still checked.
# -Body: the editor's character model (.glb), shipped as files\Editor\body.glb. It is not in Git and not in the source
# zip (not covered by the GPL). -NoBody builds a package without it (the editor then shows its built-in mannequin).
param([string]$Dll = "", [string]$Exe = "", [string]$Body = "", [switch]$NoBody)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$root = Split-Path -Parent $PSScriptRoot
$ue4ssZip = "$root\release\cache\UE4SS_v3.0.1.zip"
$ue4ssSha = "4B47D4BCEDDD2F561A4E395BFA00924CCFC945AF576A2D0C613E6537846C57EC"   # downloaded and checked 2026-11-27

function Stop-Release([string]$m) { throw "release: $m" }
function Find-Number([string]$file, [string]$pattern) {
    $m = [regex]::Match((Get-Content -LiteralPath "$root\$file" -Raw), $pattern)
    if (-not $m.Success) { Stop-Release "version not found in $file" }
    return [int]$m.Groups[1].Value
}

# ---------------------------------------------------------------- versions must agree
$n = Find-Number "SessionAnimLoader\Native\main.cpp" 'kVersion\s*=\s*(\d+)'
$nLua = Find-Number "SessionAnimLoader\Lua\Scripts\main.lua" 'local VERSION = "v0\.(\d+)"'
$nRc = Find-Number "SessionStudio\app\res\SessionStudio.rc" 'FILEVERSION 0,(\d+),0,0'
if ($nLua -ne $n -or $nRc -ne $n) { Stop-Release "versions differ: DLL v0.$n, Lua v0.$nLua, editor resource v0.$nRc" }
$name = "SessionStudio-v0.$n"

# (PowerShell names ignore case: $Dll and $Exe are the parameters above)
if (-not $Dll) { $Dll = "$root\SessionAnimLoader\Native\dist\main.dll" }
if (-not $Exe) { $Exe = "$root\SessionStudio\dist\SessionStudio.exe" }
foreach ($f in $Dll, $Exe) { if (-not (Test-Path -LiteralPath $f)) { Stop-Release "$f is missing: run build.cmd" } }
$Dll = (Get-Item -LiteralPath $Dll).FullName
$Exe = (Get-Item -LiteralPath $Exe).FullName
if (-not $NoBody) {
    if (-not $Body) { $Body = "$root\SessionStudio\AMXX_SkelMesh_Full.glb" }
    if (-not (Test-Path -LiteralPath $Body -PathType Leaf)) { Stop-Release "$Body is missing: give -Body <model.glb>, or -NoBody" }
    $Body = (Get-Item -LiteralPath $Body).FullName
}
$exeVer = (Get-Item -LiteralPath $Exe).VersionInfo.FileVersion
if ($exeVer -ne "0.$n.0.0") { Stop-Release "SessionStudio.exe says version '$exeVer', expected 0.$n.0.0: run build.cmd" }
if (-not (Test-Path -LiteralPath $ue4ssZip)) { Stop-Release "put the official UE4SS_v3.0.1.zip in release\cache (see the top of this script)" }
if ((Get-FileHash -LiteralPath $ue4ssZip).Hash -ne $ue4ssSha) { Stop-Release "release\cache\UE4SS_v3.0.1.zip is not the checked file (SHA-256 differs)" }

$out = "$root\release\out"
$stage = "$out\$name"
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

function Add-File([string]$from, [string]$to) {
    if (-not (Test-Path -LiteralPath $from -PathType Leaf)) { Stop-Release "missing file: $from" }
    $dst = "$stage\$to"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    Copy-Item -LiteralPath $from -Destination $dst
}
function Add-Text([string]$to, [string]$text) {   # UTF-8 without BOM, Windows line ends
    $dst = "$stage\$to"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    $t = ($text -replace "`r`n", "`n") -replace "`n", "`r`n"
    [System.IO.File]::WriteAllText($dst, $t, (New-Object System.Text.UTF8Encoding($false)))
}
function Add-TextFile([string]$from, [string]$to) { Add-Text $to ([System.IO.File]::ReadAllText($from)) }

# ---------------------------------------------------------------- the top of the package
Add-TextFile "$root\installer\INSTALLER.cmd" "INSTALLER.cmd"
Add-TextFile "$root\installer\README.txt" "README.txt"
Add-TextFile "$root\INSTALL.md" "INSTALL.md"
Add-TextFile "$root\LICENSE" "LICENSE.txt"
Add-TextFile "$root\THIRD-PARTY-NOTICES.txt" "THIRD-PARTY-NOTICES.txt"
Add-TextFile "$root\installer\UNINSTALL.cmd" "UNINSTALL.cmd"
foreach ($s in "install.ps1", "uninstall.ps1", "common.ps1") { Add-TextFile "$root\installer\$s" "files\$s" }

# ---------------------------------------------------------------- the game mod
$game = "files\Game\Mods"
Add-File $dll "$game\SessionAnimLoaderNative\dlls\main.dll"
Add-Text "$game\SessionAnimLoaderNative\enabled.txt" ""
Add-File "$root\SessionAnimLoader\Lua\Scripts\main.lua" "$game\SessionAnimLoader\Scripts\main.lua"
Add-File "$root\SessionAnimLoader\Lua\enabled.txt" "$game\SessionAnimLoader\enabled.txt"
# no clip and no recording; this note keeps clips\others in the zip (for a copy by hand)
Add-Text "$game\SessionAnimLoader\clips\others\README.txt" "Other players' clips go here (multiplayer only). They are never played on your own skater.`n"

# ---------------------------------------------------------------- UE4SS 3.0.1: the loader only, none of its sample mods
$tmp = "$out\ue4ss-tmp"
[System.IO.Compression.ZipFile]::ExtractToDirectory($ue4ssZip, $tmp)
Add-File "$tmp\UE4SS.dll" "files\UE4SS\UE4SS.dll"
Add-File "$tmp\dwmapi.dll" "files\UE4SS\dwmapi.dll"
$ini = [System.IO.File]::ReadAllText("$tmp\UE4SS-settings.ini")
# the values of the setup the mod was tested with: engine 4.27, no developer console
foreach ($kv in @(@("MajorVersion", "4"), @("MinorVersion", "27"), @("GuiConsoleEnabled", "0"), @("DumpOffsetsAndSizes", "0"))) {
    $re = New-Object System.Text.RegularExpressions.Regex("(?m)^$($kv[0])[ \t]*=[^\r\n]*")
    if ($re.Matches($ini).Count -ne 1) { Stop-Release "UE4SS-settings.ini: expected one '$($kv[0])' line" }
    $ini = $re.Replace($ini, "$($kv[0]) = $($kv[1])")
}
Add-Text "files\UE4SS\UE4SS-settings.ini" $ini
Add-Text "files\UE4SS\Mods\mods.txt" "; UE4SS mod list. Session Anim Loader needs no line here: each of its two folders has an enabled.txt.`n"
Remove-Item -LiteralPath $tmp -Recurse -Force

# ---------------------------------------------------------------- the editor and its guides
Add-File $exe "files\Editor\SessionStudio.exe"
if (-not $NoBody) { Add-File $Body "files\Editor\body.glb" }
Add-TextFile "$root\LICENSE" "files\Editor\LICENSE.txt"
Add-TextFile "$root\THIRD-PARTY-NOTICES.txt" "files\Editor\THIRD-PARTY-NOTICES.txt"
foreach ($g in "Getting-Started", "Reference") { Add-TextFile "$root\SessionStudio\docs\$g.md" "files\Editor\docs\$g.md" }
Add-TextFile "$root\INSTALL.md" "files\Editor\docs\INSTALL.md"   # uninstall and troubleshooting stay at hand once the zip is gone
& node "$root\SessionStudio\docs\md2html.js" "$stage\files\Editor\docs" | Out-Null
if ($LASTEXITCODE -ne 0) { Stop-Release "md2html failed on the guides" }
& node "$root\SessionStudio\docs\md2html.js" "$stage" | Out-Null   # INSTALL.html
if ($LASTEXITCODE -ne 0) { Stop-Release "md2html failed on INSTALL.md" }

# ---------------------------------------------------------------- source code of this version (GPL-3.0)
# the working copy as Git sees it (tracked and new files, ignored files left out), without the project's internal
# working documents, plus the two libraries compiled into the editor
Push-Location $root
try { $listed = @(& git ls-files --cached --others --exclude-standard) } finally { Pop-Location }
if ($LASTEXITCODE -ne 0 -or $listed.Count -lt 20) { Stop-Release "git ls-files failed" }
$internal = "CLAUDE.md", "PROJECT_STATUS.md", "DEVELOPMENT_LOG.md"
$source = @($listed | Where-Object { $internal -notcontains $_ -and (Test-Path -LiteralPath "$root\$_" -PathType Leaf) })
$imgui = "SessionStudio/third_party/imgui"
$gizmo = "SessionStudio/third_party/ImGuizmo"
$libs = @(Get-ChildItem -LiteralPath "$root\$imgui" -File | Where-Object { $_.Extension -in ".cpp", ".h" } | ForEach-Object { "$imgui/$($_.Name)" })
$libs += "$imgui/LICENSE.txt"
foreach ($b in "imgui_impl_win32", "imgui_impl_dx11") { $libs += "$imgui/backends/$b.cpp", "$imgui/backends/$b.h" }
$libs += "$gizmo/LICENSE", "$gizmo/src/ImGuizmo.cpp", "$gizmo/src/ImGuizmo.h"
$source += $libs
$srcZip = "$out\$name-source.zip"

function New-Zip([string]$zipPath, [array]$entries) {   # entries: @(full path, name inside the zip with / separators)
    $z = [System.IO.Compression.ZipFile]::Open($zipPath, "Create")
    try {
        foreach ($e in $entries) { [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, $e[0], $e[1], "Optimal") }
    } finally { $z.Dispose() }
    # read it back: every file must be there under its own name
    $z = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $names = @($z.Entries | ForEach-Object { $_.FullName })
        $want = @($entries | ForEach-Object { $_[1] })
        if ($names.Count -ne $want.Count -or (Compare-Object $names $want) -or @($names | Sort-Object -Unique).Count -ne $names.Count -or @($names | Where-Object { $_.EndsWith("/") }).Count) {
            Stop-Release "$zipPath does not hold the expected file names"
        }
    } finally { $z.Dispose() }
}
New-Zip $srcZip @($source | Sort-Object | ForEach-Object { ,@("$root\$($_ -replace '/', '\')", "$name-source/$_") })

# ---------------------------------------------------------------- nothing personal, nothing recorded
# the current Windows user name (the public handle "<name>-Dev" is allowed), a user-profile path, a personal mailbox
$user = [regex]::Escape($env:USERNAME)
$private = "(?i)($user(?!-Dev)|[A-Z]:[\\/]+Users[\\/]+(?!<)[A-Za-z0-9_.]+|[A-Za-z0-9._+-]+@(gmail|outlook|hotmail|yahoo)\.[a-z]+)"
$latin1 = [System.Text.Encoding]::GetEncoding(28591)
function Find-Private([byte[]]$bytes) {
    foreach ($enc in $latin1, [System.Text.Encoding]::Unicode) {
        $m = [regex]::Match($enc.GetString($bytes), $private)
        if ($m.Success) { return $m.Value }
    }
    return $null
}
$bad = @()
foreach ($f in Get-ChildItem -LiteralPath $stage -Recurse -File) {
    # UE4SS's own files are the upstream release, byte for byte
    if ($f.FullName -like "*\files\UE4SS\*.dll") { continue }
    $hit = Find-Private ([System.IO.File]::ReadAllBytes($f.FullName))
    if ($hit) { $bad += "$($f.FullName.Substring($stage.Length + 1)): $hit" }
}
foreach ($s in $source) {
    $hit = Find-Private ([System.IO.File]::ReadAllBytes("$root\$($s -replace '/', '\')"))
    if ($hit) { $bad += "source $($s): $hit" }
}
if ($bad.Count) { $bad | ForEach-Object { Write-Host "  PERSONAL: $_" }; Stop-Release "personal data found" }
$forbidden = "(?i)(\.sanim$|\.log$|\.ini$|\.dmp$|\.pdb$|selftest|_figures|_skeleton|_trick_names|captures[\\/]|backups?[\\/])"
$wrong = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stage.Length + 1) } |
    Where-Object { $_ -match $forbidden -and $_ -ne "files\UE4SS\UE4SS-settings.ini" })
$wrong += @($source | Where-Object { $_ -match "(?i)(\.glb$|\.sanim$|\.log$|\.ini$|\.exe$|\.dll$|\.zip$|_figures|_skeleton\.json|^notes/|^release/|^_archive/)" })
if ($wrong.Count) { $wrong | ForEach-Object { Write-Host "  FORBIDDEN: $_" }; Stop-Release "files that must never be shipped" }

# ---------------------------------------------------------------- the player package
$pkgZip = "$out\$name.zip"
New-Zip $pkgZip @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName |
    ForEach-Object { ,@($_.FullName, ("$name/" + ($_.FullName.Substring($stage.Length + 1) -replace '\\', '/'))) })
$sums = foreach ($z in $pkgZip, $srcZip) { "$((Get-FileHash -LiteralPath $z).Hash.ToLowerInvariant())  $(Split-Path -Leaf $z)" }
[System.IO.File]::WriteAllText("$out\SHA256SUMS.txt", (($sums -join "`r`n") + "`r`n"))

Write-Host "`nPackage content:"
foreach ($f in Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName) {
    Write-Host ("{0,10}  {1}  {2}" -f $f.Length, (Get-FileHash -LiteralPath $f.FullName).Hash.Substring(0, 16), $f.FullName.Substring($stage.Length + 1))
}
Write-Host "`nSource zip: $($source.Count) files"
$sums | ForEach-Object { Write-Host $_ }
Write-Host "personal data check: OK; forbidden files check: OK"
