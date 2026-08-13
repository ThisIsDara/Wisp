# generate-shadow.ps1 - one-shot pre-rendered shadow asset generator (reproducibility only)
# Produces qml/assets/shadow.png: 672x432, soft 16px shadow halo around a 640x400 rounded rect.
# Render at 4x then downscale with HighQualityBicubic = soft pre-blurred edge, zero runtime blur.

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$targetW = 672
$targetH = 432
$scale = 4
$shadowAlpha = 115   # ~45% of 255

$bitmap = New-Object System.Drawing.Bitmap -ArgumentList (($targetW * $scale), ($targetH * $scale), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bitmap)
$g.Clear([System.Drawing.Color]::Transparent)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

$black = [System.Drawing.Color]::FromArgb($shadowAlpha, 0, 0, 0)
$brush = New-Object System.Drawing.SolidBrush($black)

# Surface region (scaled): 640x400 at 16px margin -> shadow rect spans the full canvas
$margin = 16 * $scale
$rect = New-Object System.Drawing.Rectangle -ArgumentList ($margin, $margin, (640 * $scale), (400 * $scale))
$radius = 12 * $scale

$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$d = $radius * 2
$path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
$path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
$path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
$path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
$path.CloseFigure()
$g.FillPath($brush, $path)

$g.Dispose()
$brush.Dispose()
$path.Dispose()

# Downscale to 672x432 with high-quality bicubic = the soft shadow edge
$final = New-Object System.Drawing.Bitmap -ArgumentList ($targetW, $targetH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g2 = [System.Drawing.Graphics]::FromImage($final)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g2.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$g2.DrawImage($bitmap, 0, 0, $targetW, $targetH)
$g2.Dispose()
$bitmap.Dispose()

$outDir = Join-Path $PSScriptRoot "..\qml\assets"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outPath = Join-Path $outDir "shadow.png"
$final.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$final.Dispose()

Write-Host "Wrote $outPath ($targetW x $targetH)"
