# Draws the Session Studio icon and writes SessionStudio\app\res\SessionStudio.ico (committed, so a build does not
# need this script). The artwork is drawn here with plain shapes: no font, no external image.
#   powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1 [-Preview <png>]
param([string]$Preview = "")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "SessionStudio\app\res\SessionStudio.ico"

function New-Art([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $u = $size / 256.0                      # the drawing is designed on a 256 x 256 grid
    $g.ScaleTransform($u, $u)

    # rounded square in the editor's accent orange (ui_theme.cpp: #FF6B1A)
    $r = 56.0; $d = 2 * $r
    $tile = New-Object System.Drawing.Drawing2D.GraphicsPath
    $tile.AddArc(8, 8, $d, $d, 180, 90)
    $tile.AddArc(248 - $d, 8, $d, $d, 270, 90)
    $tile.AddArc(248 - $d, 248 - $d, $d, $d, 0, 90)
    $tile.AddArc(8, 248 - $d, $d, $d, 90, 90)
    $tile.CloseFigure()
    $fill = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF(0, 0)), (New-Object System.Drawing.PointF(0, 256)),
        [System.Drawing.Color]::FromArgb(255, 255, 130, 48), [System.Drawing.Color]::FromArgb(255, 240, 90, 10))
    $g.FillPath($fill, $tile)

    $ink = [System.Drawing.Color]::FromArgb(255, 22, 24, 29)   # dark anthracite
    # at 24 px and below only the "S" stays, larger and thicker, so it remains readable
    $small = $size -le 24
    $s = New-Object System.Drawing.Drawing2D.GraphicsPath
    $s.AddBezier(170, 66, 150, 40, 84, 42, 84, 82)
    $s.AddBezier(84, 82, 84, 122, 174, 108, 174, 150)
    $s.AddBezier(174, 150, 174, 192, 106, 196, 82, 166)
    $pen = New-Object System.Drawing.Pen($ink, $(if ($small) { 40.0 } else { 30.0 }))
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $state = $g.Save()
    if ($small) { $g.TranslateTransform(0, 12) } else { $g.TranslateTransform(0, -10) }
    $g.DrawPath($pen, $s)
    $g.Restore($state)

    if (-not $small) {
        # a skateboard seen from the side, under the letter: deck with kicked ends, two wheels
        $deck = New-Object System.Drawing.Drawing2D.GraphicsPath
        $deck.AddBezier(50, 193, 62, 207, 68, 207, 82, 207)
        $deck.AddLine(82, 207, 174, 207)
        $deck.AddBezier(174, 207, 188, 207, 194, 207, 206, 193)
        $deckPen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 13.0)
        $deckPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $deckPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $g.DrawPath($deckPen, $deck)
        $white = [System.Drawing.Brushes]::White
        $g.FillEllipse($white, 80, 216, 20, 20)
        $g.FillEllipse($white, 156, 216, 20, 20)
        $deckPen.Dispose()
    }
    $pen.Dispose(); $fill.Dispose(); $g.Dispose()
    return $bmp
}

function Get-PngBytes($bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return ,$ms.ToArray()
}

# classic icon image: BITMAPINFOHEADER, 32-bit pixels bottom-up, then an empty 1-bit mask (alpha does the job)
function Get-DibBytes($bmp) {
    $n = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter($ms)
    $w.Write([uint32]40); $w.Write([int32]$n); $w.Write([int32]($n * 2))
    $w.Write([uint16]1); $w.Write([uint16]32)
    foreach ($i in 1..6) { $w.Write([uint32]0) }
    for ($y = $n - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $n; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $w.Write([byte]$c.B); $w.Write([byte]$c.G); $w.Write([byte]$c.R); $w.Write([byte]$c.A)
        }
    }
    $maskRow = [int]([math]::Floor(($n + 31) / 32)) * 4
    $w.Write((New-Object byte[] ($maskRow * $n)))
    $w.Flush()
    return ,$ms.ToArray()
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = @()
foreach ($sz in $sizes) {
    $bmp = New-Art $sz
    $bytes = if ($sz -ge 128) { Get-PngBytes $bmp } else { Get-DibBytes $bmp }
    $images += ,@{ size = $sz; bytes = $bytes }
    $bmp.Dispose()
}

New-Item -ItemType Directory -Force (Split-Path -Parent $out) | Out-Null
$fs = [System.IO.File]::Create($out)
$w = New-Object System.IO.BinaryWriter($fs)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($im in $images) {
    $dim = if ($im.size -ge 256) { 0 } else { $im.size }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$im.bytes.Length); $w.Write([uint32]$offset)
    $offset += $im.bytes.Length
}
foreach ($im in $images) { $w.Write([byte[]]$im.bytes) }
$w.Close(); $fs.Close()
Write-Host "written: $out ($((Get-Item $out).Length) bytes, sizes $($sizes -join ', '))"

if ($Preview) {
    # every size side by side on a dark and a light background, to check it stays readable
    $sheet = New-Object System.Drawing.Bitmap(720, 560)
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 32, 34, 40))
    $g.FillRectangle([System.Drawing.Brushes]::WhiteSmoke, 0, 280, 720, 280)
    foreach ($row in 0, 280) {
        $b = New-Art 256; $g.DrawImageUnscaled($b, 10, $row + 12); $b.Dispose()
        $x = 290
        foreach ($sz in 64, 48, 40, 32, 24, 20, 16) {
            $b = New-Art $sz; $g.DrawImageUnscaled($b, $x, $row + 100); $b.Dispose(); $x += $sz + 14
        }
    }
    $g.Dispose(); $sheet.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png); $sheet.Dispose()
    Write-Host "preview: $Preview"
}
