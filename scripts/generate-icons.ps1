# generate-icons.ps1 - 20 wisp/orb icon candidates + numbered contact sheet
# Each candidate is rendered at 256px and packed into a multi-size .ico
# (16/24/32/48/64/256, PNG-compressed entries). The chosen one is copied
# over assets\wisp.ico (the wisp.exe embedded icon).
# Gallery: assets\icon-gallery.png — pick a number 1..20.
param()
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$repo = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $repo "assets\icons"
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

# palette
$Bg      = [System.Drawing.Color]::FromArgb(255, 36, 41, 51)   # #242933
$C_Blue   = [System.Drawing.Color]::FromArgb(255, 122, 162, 247) # #7AA2F7
$C_Cyan   = [System.Drawing.Color]::FromArgb(255, 125, 211, 252)
$C_Violet = [System.Drawing.Color]::FromArgb(255, 167, 139, 250)
$C_Amber  = [System.Drawing.Color]::FromArgb(255, 252, 211, 77)
$C_Orange = [System.Drawing.Color]::FromArgb(255, 253, 186, 116)
$C_Emerald= [System.Drawing.Color]::FromArgb(255, 110, 231, 183)
$C_Magenta= [System.Drawing.Color]::FromArgb(255, 240, 171, 252)
$C_Rose   = [System.Drawing.Color]::FromArgb(255, 244, 114, 182)
$C_Ice    = [System.Drawing.Color]::FromArgb(255, 165, 243, 252)
$C_Gold   = [System.Drawing.Color]::FromArgb(255, 253, 230, 138)
$C_White  = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)

function New-Canvas([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    return @{ bmp = $bmp; g = $g }
}

function Add-Background($g, [int]$size) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 48.0 * $size / 256.0
    $d = 2.0 * $r
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($size - $d, 0, $d, $d, 270, 90)
    $path.AddArc($size - $d, $size - $d, $d, $d, 0, 90)
    $path.AddArc(0, $size - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $g.FillPath((New-Object System.Drawing.SolidBrush($Bg)), $path)
    $path.Dispose()
}

# soft halo: concentric translucent circles (outermost faintest)
function Add-Glow($g, [int]$size, [double]$cx, [double]$cy, [double]$r, [System.Drawing.Color]$color) {
    $u = $size / 256.0
    $layers = @(
        @{ a = 14; s = 3.1 },
        @{ a = 22; s = 2.3 },
        @{ a = 36; s = 1.7 },
        @{ a = 60; s = 1.25 },
        @{ a = 95; s = 1.02 }
    )
    foreach ($L in $layers) {
        $br = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb($L.a, $color.R, $color.G, $color.B))
        $rad = $r * $u * $L.s
        $g.FillEllipse($br, [float]($cx * $u - $rad), [float]($cy * $u - $rad), [float](2 * $rad), [float](2 * $rad))
        $br.Dispose()
    }
}

function Add-Sphere($g, [int]$size, [double]$cx, [double]$cy, [double]$r, [System.Drawing.Color]$bright, [System.Drawing.Color]$dark) {
    $u = $size / 256.0
    $rect = New-Object System.Drawing.RectangleF([float](($cx - $r) * $u), [float](($cy - $r) * $u), [float](2 * $r * $u), [float](2 * $r * $u))
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $bright, $dark, [single]35)
    $g.FillEllipse($grad, $rect)
    $grad.Dispose()
    # top-left highlight
    $hl = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(150, 255, 255, 255))
    $g.FillEllipse($hl, [float](($cx - 0.42 * $r) * $u), [float](($cy - 0.52 * $r) * $u), [float](0.38 * $r * $u), [float](0.30 * $r * $u))
    $hl.Dispose()
}

function Add-FlamePath($g, [int]$size, [double]$cx, [double]$cy, [double]$r, [System.Drawing.Color]$bright, [System.Drawing.Color]$dark) {
    $u = $size / 256.0
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $x0 = $cx * $u; $y0 = ($cy - 1.42 * $r) * $u                 # tip
    $xb = $cx * $u; $yb = ($cy + 0.78 * $r) * $u                 # bottom center
    $l = ($cx - 0.72 * $r) * $u; $rgt = ($cx + 0.72 * $r) * $u
    $ym = ($cy + 0.10 * $r) * $u                                 # belly height
    $p.AddBezier($x0, $y0, ($cx - 0.42 * $r) * $u, ($cy - 0.62 * $r) * $u, $l, ($cy - 0.30 * $r) * $u, $l, $ym)
    $p.AddBezier($l, $ym, $l, ($cy + 0.72 * $r) * $u, $xb, ($cy + 1.05 * $r) * $u, $xb, $yb)
    $p.AddBezier($xb, $yb, $xb, ($cy + 1.05 * $r) * $u, $rgt, ($cy + 0.72 * $r) * $u, $rgt, $ym)
    $p.AddBezier($rgt, $ym, $rgt, ($cy - 0.30 * $r) * $u, ($cx + 0.42 * $r) * $u, ($cy - 0.62 * $r) * $u, $x0, $y0)
    $p.CloseFigure()
    $rect = New-Object System.Drawing.RectangleF([float](($cx - $r) * $u), [float](($cy - 1.45 * $r) * $u), [float](2 * $r * $u), [float](2.5 * $r * $u))
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $bright, $dark, [single]-40)
    $g.FillPath($grad, $p)
    $grad.Dispose(); $p.Dispose()
}

function Add-Star4($g, [int]$size, [double]$cx, [double]$cy, [double]$r, [System.Drawing.Color]$color, [int]$alpha = 255) {
    $u = $size / 256.0
    $pts = New-Object System.Drawing.PointF[](8)
    $a = @(0, 0.22, 1, 0.22, 0, -0.22, -1, -0.22)
    $b = @(-1, -0.22, 0, -0.22, 1, 0.22, 0, 0.22)  # unused mirror; build octagon:
    $shape = @(
        @(0.0, -1.0), @(0.22, -0.22), @(1.0, 0.0), @(0.22, 0.22),
        @(0.0, 1.0), @(-0.22, 0.22), @(-1.0, 0.0), @(-0.22, -0.22)
    )
    for ($i = 0; $i -lt 8; $i++) {
        $pts[$i] = New-Object System.Drawing.PointF([float](($cx + $shape[$i][0] * $r) * $u), [float](($cy + $shape[$i][1] * $r) * $u))
    }
    $br = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb($alpha, $color.R, $color.G, $color.B))
    $g.FillPolygon($br, $pts)
    $br.Dispose()
}

# 4-point star glyphs — convenience
function Add-Sparkle($g, [int]$size, [double]$cx, [double]$cy, [double]$r, [System.Drawing.Color]$color) {
    Add-Star4 $g $size $cx $cy $r $color
}

# pack a 256px bitmap into a multi-size .ico
function Save-Ico($bmp256, [string]$name) {
    $sizes = @(16, 24, 32, 48, 64, 256)
    $pngs = @()
    foreach ($s in $sizes) {
        $bmp = if ($s -eq 256) { $bmp256 } else {
            $small = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $gg = [System.Drawing.Graphics]::FromImage($small)
            $gg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $gg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $gg.DrawImage($bmp256, 0, 0, $s, $s)
            $gg.Dispose()
            $small
        }
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $pngs += , $ms.ToArray()
        $ms.Dispose()
        if ($s -ne 256) { $bmp.Dispose() }
    }
    $out = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($out)
    $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $s = $sizes[$i]
        $bw.Write([byte]($s -band 0xFF)); $bw.Write([byte]($s -band 0xFF))
        $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Write([uint16]1); $bw.Write([uint16]32)
        $bw.Write([uint32]$pngs[$i].Length); $bw.Write([uint32]$offset)
        $offset += $pngs[$i].Length
    }
    foreach ($p in $pngs) { $bw.Write($p) }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes((Join-Path $outDir "$name.ico"), $out.ToArray())
    $bw.Dispose(); $out.Dispose()
}

$icons = @()

# 01 GlowOrb-Blue
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 72 $C_Blue
Add-Sphere $c.g 256 128 128 62 $C_White $C_Blue
$icons += , @{ name = "wisp-01"; canvas = $c }

# 02 WispFlame-Blue
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 132 78 $C_Blue
Add-FlamePath $c.g 256 128 132 60 $C_White $C_Blue
$icons += , @{ name = "wisp-02"; canvas = $c }

# 03 CometWisp-Cyan
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 150 100 58 $C_Cyan
$tail = New-Object System.Drawing.Drawing2D.GraphicsPath
$tpts = New-Object System.Drawing.PointF[](3)
$tpts[0] = New-Object System.Drawing.PointF([float]110, [float]130)
$tpts[1] = New-Object System.Drawing.PointF([float]40, [float]175)
$tpts[2] = New-Object System.Drawing.PointF([float]70, [float]70)
$tail.AddPolygon($tpts)
$tb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(90, 125, 211, 252))
$c.g.FillPath($tb, $tail); $tb.Dispose(); $tail.Dispose()
Add-Sphere $c.g 256 150 100 52 $C_White $C_Cyan
Add-Sparkle $c.g 256 68 62 10 $C_Cyan
$icons += , @{ name = "wisp-03"; canvas = $c }

# 04 TwinWisps
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 100 130 48 $C_Blue
Add-Glow $c.g 256 156 126 42 $C_Magenta
Add-Sphere $c.g 256 100 130 40 $C_White $C_Blue
Add-Sphere $c.g 256 156 126 34 $C_White $C_Magenta
Add-Sparkle $c.g 256 64 78 9 $C_Blue
$icons += , @{ name = "wisp-04"; canvas = $c }

# 05 AuraOrb-Violet
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 70 $C_Violet
Add-Sphere $c.g 256 128 128 50 $C_White $C_Violet
$ring = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(220, 255, 255, 255), [float](4.5))
$ring2 = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(70, 167, 139, 250), [float](9))
$c.g.DrawEllipse($ring2, 84, 84, 88, 88)
$c.g.DrawEllipse($ring, 94, 94, 68, 68)
$ring.Dispose(); $ring2.Dispose()
$icons += , @{ name = "wisp-05"; canvas = $c }

# 06 LanternStar-White
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 66 $C_White
Add-Sphere $c.g 256 128 128 56 $C_White ([System.Drawing.Color]::FromArgb(255, 160, 175, 200))
Add-Sparkle $c.g 256 196 56 16 $C_White
$icons += , @{ name = "wisp-06"; canvas = $c }

# 07 CrescentMoon-White
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 138 128 60 $C_White
$outer = New-Object System.Drawing.SolidBrush($C_White)
$c.g.FillEllipse($outer, 86, 76, 104, 104); $outer.Dispose()
$punch = New-Object System.Drawing.SolidBrush($Bg)
$c.g.FillEllipse($punch, 108, 66, 100, 100); $punch.Dispose()
$icons += , @{ name = "wisp-07"; canvas = $c }

# 08 Firefly-Amber
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 116 140 50 $C_Amber
Add-Sphere $c.g 256 116 140 40 $C_White $C_Amber
Add-Glow $c.g 256 188 82 16 $C_Amber
Add-Sphere $c.g 256 188 82 12 $C_White $C_Amber
Add-Glow $c.g 256 74 62 11 $C_Amber
Add-Sphere $c.g 256 74 62 8 $C_White $C_Amber
Add-Sparkle $c.g 256 116 140 12 $C_White
$icons += , @{ name = "wisp-08"; canvas = $c }

# 09 NebulaOrb-Magenta
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 74 $C_Magenta
Add-Sphere $c.g 256 128 128 58 $C_White $C_Magenta
$blob = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(90, 255, 255, 255))
$c.g.FillEllipse($blob, 108, 96, 34, 22)
$c.g.FillEllipse($blob, 138, 140, 28, 18)
$c.g.FillEllipse($blob, 92, 138, 22, 16)
$blob.Dispose()
$icons += , @{ name = "wisp-09"; canvas = $c }

# 10 MagicEye-Cyan
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 68 $C_Cyan
Add-Sphere $c.g 256 128 128 56 $C_White $C_Cyan
$pupil = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 24, 32, 48))
$c.g.FillEllipse($pupil, 112, 112, 32, 32); $pupil.Dispose()
$spark = New-Object System.Drawing.SolidBrush($C_White)
$c.g.FillEllipse($spark, 118, 118, 10, 10); $spark.Dispose()
$icons += , @{ name = "wisp-10"; canvas = $c }

# 11 PulseRings-Blue
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 60 $C_Blue
Add-Sphere $c.g 256 128 128 44 $C_White $C_Blue
$pr1 = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(55, 122, 162, 247), [float](7))
$pr2 = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(30, 122, 162, 247), [float](7))
$c.g.DrawEllipse($pr1, 72, 72, 112, 112)
$c.g.DrawEllipse($pr2, 44, 44, 168, 168)
$pr1.Dispose(); $pr2.Dispose()
$icons += , @{ name = "wisp-11"; canvas = $c }

# 12 StarTrail-White
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 158 100 50 $C_White
Add-Sphere $c.g 256 158 100 42 $C_White ([System.Drawing.Color]::FromArgb(255, 150, 165, 195))
Add-Sparkle $c.g 256 92 88 11 $C_White
Add-Sparkle $c.g 256 60 128 7 $C_White
Add-Sparkle $c.g 256 92 158 9 $C_White
Add-Sparkle $c.g 256 160 168 6 $C_White
$icons += , @{ name = "wisp-12"; canvas = $c }

# 13 Bifrost-Violet
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 58 $C_Violet
$t1 = New-Object System.Drawing.Drawing2D.GraphicsPath
$tp1 = New-Object System.Drawing.PointF[](3)
$tp1[0] = New-Object System.Drawing.PointF([float]96, [float]96)
$tp1[1] = New-Object System.Drawing.PointF([float]48, [float]48)
$tp1[2] = New-Object System.Drawing.PointF([float]70, [float]110)
$t1.AddPolygon($tp1)
$tb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(80, 167, 139, 250))
$c.g.FillPath($tb, $t1)
$t2 = New-Object System.Drawing.Drawing2D.GraphicsPath
$tp2 = New-Object System.Drawing.PointF[](3)
$tp2[0] = New-Object System.Drawing.PointF([float]160, [float]160)
$tp2[1] = New-Object System.Drawing.PointF([float]208, [float]208)
$tp2[2] = New-Object System.Drawing.PointF([float]146, [float]186)
$t2.AddPolygon($tp2)
$c.g.FillPath($tb, $t2)
$tb.Dispose(); $t1.Dispose(); $t2.Dispose()
Add-Sphere $c.g 256 128 128 50 $C_White $C_Violet
$icons += , @{ name = "wisp-13"; canvas = $c }

# 14 HaloDisc-Gold
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 64 $C_Gold
$disc = New-Object System.Drawing.SolidBrush($C_Gold)
$c.g.FillEllipse($disc, 92, 92, 72, 72); $disc.Dispose()
$hr = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(210, 255, 255, 255), [float](6))
$c.g.DrawEllipse($hr, 66, 66, 124, 124); $hr.Dispose()
$hd = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
$c.g.FillEllipse($hd, 122, 122, 12, 12); $hd.Dispose()
$icons += , @{ name = "wisp-14"; canvas = $c }

# 15 ChargedOrb-Emerald
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 66 $C_Emerald
Add-Sphere $c.g 256 128 128 52 $C_White $C_Emerald
$bolt = New-Object System.Drawing.Pen($C_White, [float](8))
$bolt.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$bolt.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$bpts = New-Object System.Drawing.PointF[](4)
$bpts[0] = New-Object System.Drawing.PointF([float]116, [float]88)
$bpts[1] = New-Object System.Drawing.PointF([float]132, [float]118)
$bpts[2] = New-Object System.Drawing.PointF([float]120, [float]118)
$bpts[3] = New-Object System.Drawing.PointF([float]140, [float]152)
$c.g.DrawLines($bolt, $bpts)
$bolt.Dispose()
$icons += , @{ name = "wisp-15"; canvas = $c }

# 16 PetalOrb-Rose
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 64 $C_Rose
Add-Sphere $c.g 256 128 128 42 $C_White $C_Rose
$pet = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(230, 255, 255, 255), [float](7))
$pet.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$pet.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
for ($k = 0; $k -lt 6; $k++) {
    $ang = $k * 60 - 90
    $rad = [Math]::PI * $ang / 180.0
    $px = 128 + 66 * [Math]::Cos($rad)
    $py = 128 + 66 * [Math]::Sin($rad)
    $cx2 = 128 + 34 * [Math]::Cos($rad)
    $cy2 = 128 + 34 * [Math]::Sin($rad)
    $c.g.DrawLine($pet, [float]$cx2, [float]$cy2, [float]$px, [float]$py)
}
$pet.Dispose()
$icons += , @{ name = "wisp-16"; canvas = $c }

# 17 StarCore-Gold
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 66 $C_Gold
Add-Sphere $c.g 256 128 128 52 $C_White $C_Gold
Add-Sparkle $c.g 256 128 128 34 $C_White
$icons += , @{ name = "wisp-17"; canvas = $c }

# 18 IceSparkle-Ice
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 70 $C_Ice
$dia = New-Object System.Drawing.SolidBrush($C_White)
# elongated diamond: top tip up, wide middle, bottom tip down
$d1 = @(
    @(128.0, 40.0), @(160.0, 118.0), @(128.0, 128.0), @(96.0, 118.0)
)
$d2 = @(
    @(128.0, 128.0), @(160.0, 138.0), @(128.0, 216.0), @(96.0, 138.0)
)
$pts1 = New-Object System.Drawing.PointF[](4)
$pts2 = New-Object System.Drawing.PointF[](4)
for ($i = 0; $i -lt 4; $i++) {
    $pts1[$i] = New-Object System.Drawing.PointF([float]$d1[$i][0], [float]$d1[$i][1])
    $pts2[$i] = New-Object System.Drawing.PointF([float]$d2[$i][0], [float]$d2[$i][1])
}
$c.g.FillPolygon($dia, $pts1); $c.g.FillPolygon($dia, $pts2); $dia.Dispose()
$core = New-Object System.Drawing.SolidBrush($C_Ice)
$c.g.FillEllipse($core, 118, 118, 20, 20); $core.Dispose()
$icons += , @{ name = "wisp-18"; canvas = $c }

# 19 RoseOrb-Swirl
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 128 70 $C_Magenta
Add-Sphere $c.g 256 128 128 56 $C_White $C_Magenta
$sw = New-Object System.Drawing.Pen($C_White, [float](8))
$sw.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$sw.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$c.g.DrawBezier($sw, 102, 108, 128, 78, 158, 108, 138, 136)
$c.g.DrawBezier($sw, 138, 136, 118, 162, 86, 140, 102, 108)
$sw.Dispose()
$icons += , @{ name = "wisp-19"; canvas = $c }

# 20 ColdFlame-Dual
$c = New-Canvas 256; Add-Background $c.g 256
Add-Glow $c.g 256 128 132 80 $C_Ice
Add-FlamePath $c.g 256 128 136 62 $C_White $C_Blue
$inner = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(160, 255, 255, 255))
$c.g.FillEllipse($inner, 112, 130, 32, 26); $inner.Dispose()
$icons += , @{ name = "wisp-20"; canvas = $c }

# ── save all + pack icos ────────────────────────────────────────────
foreach ($ic in $icons) {
    $bmp = $ic.canvas.bmp
    $bmp.Save((Join-Path $outDir "$($ic.name).png"), [System.Drawing.Imaging.ImageFormat]::Png)
    Save-Ico $bmp $ic.name
    $ic.canvas.g.Dispose(); $bmp.Dispose()
}

# ── contact sheet 5x4 with numbers ──────────────────────────────────
$cols = 5; $rows = 4
$cellW = 210; $cellH = 230
$sheetW = $cols * $cellW; $sheetH = $rows * $cellH
$sheet = New-Object System.Drawing.Bitmap($sheetW, $sheetH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$sg = [System.Drawing.Graphics]::FromImage($sheet)
$sg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$sg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$sg.Clear([System.Drawing.Color]::FromArgb(255, 42, 45, 53))
$font = New-Object System.Drawing.Font("Segoe UI", 20, [System.Drawing.FontStyle]::Bold)
$nfont = New-Object System.Drawing.Font("Segoe UI", 15, [System.Drawing.FontStyle]::Regular)
$labelBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 154, 163, 178))
$fmt = New-Object System.Drawing.StringFormat
$fmt.Alignment = [System.Drawing.StringAlignment]::Center
$fmt.LineAlignment = [System.Drawing.StringAlignment]::Center

for ($i = 0; $i -lt $icons.Count; $i++) {
    $col = $i % $cols; $row = [Math]::Floor($i / $cols)
    $x = $col * $cellW; $y = $row * $cellH
    $ic = $icons[$i]
    $bmp = New-Object System.Drawing.Bitmap((Join-Path $outDir "$($ic.name).png"))
    $drawSize = 170
    $dx = $x + ($cellW - $drawSize) / 2
    $dy = $y + 14
    $sg.DrawImage($bmp, [float]$dx, [float]$dy, [float]$drawSize, [float]$drawSize)
    $bmp.Dispose()
    $label = "$($i + 1). $($ic.name -replace 'wisp-','')"
    $labelRect = New-Object System.Drawing.RectangleF([float]$x, [float]($y + $cellH - 44), [float]$cellW, [float]36)
    $sg.DrawString($label, $nfont, $labelBrush, $labelRect, $fmt)
}
$sheet.Save((Join-Path $repo "assets\icon-gallery.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$sg.Dispose(); $sheet.Dispose()

Write-Host "Generated $($icons.Count) icons in assets\icons\ (png + ico each)"
Write-Host "Contact sheet: assets\icon-gallery.png — pick 1..$($icons.Count)"
exit 0
