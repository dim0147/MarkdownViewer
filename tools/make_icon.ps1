# Generates res\icon.ico - a dark rounded square with a white "M" and down arrow.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$outDir = Join-Path $PSScriptRoot '..\res'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
$outFile = Join-Path $outDir 'icon.ico'

$sizes = 16, 24, 32, 48, 64, 128, 256
$images = @()

foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    # rounded-rect background
    $pad = [Math]::Max(0, [int]($s * 0.03))
    $r = [Math]::Max(2, [int]($s * 0.2))
    $x = $pad; $y = $pad; $w = $s - 2 * $pad; $h = $s - 2 * $pad
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x, $y, $r, $r, 180, 90)
    $path.AddArc($x + $w - $r, $y, $r, $r, 270, 90)
    $path.AddArc($x + $w - $r, $y + $h - $r, $r, $r, 0, 90)
    $path.AddArc($x, $y + $h - $r, $r, $r, 90, 90)
    $path.CloseFigure()
    $bg = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 36, 41, 47))
    $g.FillPath($bg, $path)

    # white "M" on the left 2/3
    $fontSize = [Math]::Max(6, $s * 0.52)
    $font = New-Object System.Drawing.Font('Segoe UI', $fontSize, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = 'Center'; $sf.LineAlignment = 'Center'
    $mRect = New-Object System.Drawing.RectangleF(0, ($s * 0.02), ($s * 0.68), $s)
    $g.DrawString('M', $font, [System.Drawing.Brushes]::White, $mRect, $sf)

    # down arrow on the right
    $cx = $s * 0.76; $top = $s * 0.32; $bot = $s * 0.68
    $shaftW = [Math]::Max(1, $s * 0.07); $headW = [Math]::Max(2, $s * 0.16)
    $white = [System.Drawing.Brushes]::White
    $g.FillRectangle($white, [single]($cx - $shaftW / 2), [single]$top, [single]$shaftW, [single](($bot - $top) * 0.55))
    $tri = @(
        (New-Object System.Drawing.PointF(($cx - $headW), ($top + ($bot - $top) * 0.5))),
        (New-Object System.Drawing.PointF(($cx + $headW), ($top + ($bot - $top) * 0.5))),
        (New-Object System.Drawing.PointF($cx, $bot))
    )
    $g.FillPolygon($white, $tri)

    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $images += , @{ Size = $s; Data = $ms.ToArray() }
    $ms.Dispose()
}

# compose .ico (PNG-compressed entries)
$fs = [System.IO.File]::Create($outFile)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $wb = if ($img.Size -ge 256) { 0 } else { $img.Size }
    $bw.Write([Byte]$wb); $bw.Write([Byte]$wb); $bw.Write([Byte]0); $bw.Write([Byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$img.Data.Length); $bw.Write([UInt32]$offset)
    $offset += $img.Data.Length
}
foreach ($img in $images) { $bw.Write($img.Data) }
$bw.Close()
Write-Host "Created $outFile"
