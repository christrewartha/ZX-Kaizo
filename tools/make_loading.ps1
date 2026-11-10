# Draws the loading screen, the picture the tape loads first so it builds
# up line by line while the game loads underneath.
#
#   tools/make_loading.ps1 [-Rom <48.rom>] [-Scale 3]
#
# The picture is painted freely in the 16 Spectrum colours, then each 8x8
# cell is cut down to the two colours the hardware allows. Cells that held
# more than two are listed, so the design can be nudged until none clash.
#
# Writes data/loading.scr, which the build passes to z88dk as the tape's
# SCREEN$, and docs/loading.png.

param(
    [string]$Rom = 'C:/Program Files (x86)/Fuse/roms/48.rom',
    [int]$Scale = 3
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$scrOut = Join-Path $root 'data/loading.scr'
$pngOut = Join-Path $root 'docs/loading.png'

# Colour indices; add 8 for bright (bright black is still black)
$BLACK = 0; $BLUE = 1; $RED = 2; $MAGENTA = 3; $GREEN = 4; $CYAN = 5; $YELLOW = 6; $WHITE = 7
$B = 8

$palette = @(0x000000, 0x0000D7, 0xD70000, 0xD700D7, 0x00D700, 0x00D7D7, 0xD7D700, 0xD7D7D7,
             0x000000, 0x0000FF, 0xFF0000, 0xFF00FF, 0x00FF00, 0x00FFFF, 0xFFFF00, 0xFFFFFF)

$img = New-Object byte[] (256 * 192)

function Plot($x, $y, $c) {
    if ($x -ge 0 -and $x -lt 256 -and $y -ge 0 -and $y -lt 192) { $img[$y * 256 + $x] = $c }
}

function Get-Px($x, $y) { $img[$y * 256 + $x] }

function Fill-Rect($x0, $y0, $w, $h, $c) {
    for ($y = $y0; $y -lt $y0 + $h; $y++) { for ($x = $x0; $x -lt $x0 + $w; $x++) { Plot $x $y $c } }
}

function Fill-Cells($cx, $cy, $w, $h, $c) { Fill-Rect ($cx * 8) ($cy * 8) ($w * 8) ($h * 8) $c }

# ---------------------------------------------------------------- sources

$font = [IO.File]::ReadAllBytes($Rom)[0x3D00..0x3FFF]   # chars 32-127

$roomSrc = [IO.File]::ReadAllText((Join-Path $root 'src/room.c'))
$gfxBlock = [regex]::Match($roomSrc, 'tile_gfx\[[^{]*\{(.*?)\};', 'Singleline').Groups[1].Value
$tiles = @([regex]::Matches($gfxBlock, '\{([^}]*)\}') | ForEach-Object {
    , @($_.Groups[1].Value -split ',' | ForEach-Object { [Convert]::ToInt32($_.Trim(), 16) })
})
$attrBlock = [regex]::Match($roomSrc, 'tile_attr\[[^{]*\{([^}]*)\}').Groups[1].Value
$tileAttr = @($attrBlock -split ',' | ForEach-Object { [Convert]::ToInt32($_.Trim(), 16) })
$T = @{ ' ' = 0; '#' = 1; 'X' = 2; '=' = 3; '^' = 4; 'v' = 5 }

$sprites = @{}
$name = $null
foreach ($line in [IO.File]::ReadAllLines((Join-Path $root 'data/sprites.txt'))) {
    if ($line -match '^\[(\w+)\]') { $name = $Matches[1]; $sprites[$name] = @() }
    elseif ($name -and $line.Length -ge 16 -and -not $line.StartsWith(';')) { $sprites[$name] += $line.Substring(0, 16) }
}

# ---------------------------------------------------------------- drawing

# A game tile in its own colours, on a cell
function Draw-Tile($cx, $cy, $ch) {
    $id = $T[[string]$ch]
    $a = $tileAttr[$id]
    $br = if ($a -band 0x40) { $B } else { 0 }
    $ink = ($a -band 7) + $br; $paper = (($a -shr 3) -band 7) + $br
    for ($l = 0; $l -lt 8; $l++) {
        for ($b = 0; $b -lt 8; $b++) {
            $on = (($tiles[$id][$l] -shr (7 - $b)) -band 1) -eq 1
            Plot ($cx * 8 + $b) ($cy * 8 + $l) $(if ($on) { $ink } else { $paper })
        }
    }
}

# Rows of map characters, top-left at cell (cx, cy); '.' leaves a cell alone
function Draw-Map($cx, $cy, [string[]]$rows) {
    for ($r = 0; $r -lt $rows.Count; $r++) {
        for ($c = 0; $c -lt $rows[$r].Length; $c++) {
            if ($rows[$r][$c] -ne '.') { Draw-Tile ($cx + $c) ($cy + $r) $rows[$r][$c] }
        }
    }
}

# Sprite pixels only, scaled up, at pixel (x, y)
function Draw-Sprite($art, $x0, $y0, $c, $scale = 1, [switch]$Mirror) {
    $rows = $sprites[$art]
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $sx = if ($Mirror) { 15 - $x } else { $x }
            if ($rows[$y][$sx] -eq '#') { Fill-Rect ($x0 + $x * $scale) ($y0 + $y * $scale) $scale $scale $c }
        }
    }
}

# ROM font text at cell (cx, cy), each pixel $sx wide and $sy tall. $rows
# gives the colour of each cell row of the text, top to bottom.
function Draw-Text([string]$text, $cx, $cy, $rows, $sx = 1, $sy = 1) {
    $rows = @($rows)
    for ($i = 0; $i -lt $text.Length; $i++) {
        $g = ([int][char]$text[$i] - 32) * 8
        $ox = ($cx + $i * $sx) * 8
        for ($l = 0; $l -lt 8; $l++) {
            for ($b = 0; $b -lt 8; $b++) {
                if ((($font[$g + $l] -shr (7 - $b)) -band 1) -eq 0) { continue }
                for ($py = $l * $sy; $py -lt ($l + 1) * $sy; $py++) {
                    $c = $rows[[Math]::Min([Math]::Floor($py / 8), $rows.Count - 1)]
                    Fill-Rect ($ox + $b * $sx) ($cy * 8 + $py) $sx 1 $c
                }
            }
        }
    }
}

function Draw-Centred([string]$text, $cy, $c) {
    Draw-Text $text ([int][Math]::Floor((32 - $text.Length) / 2)) $cy $c
}

function Fill-Circle($cx, $cy, $r, $c) {
    for ($y = -$r; $y -le $r; $y++) {
        for ($x = -$r; $x -le $r; $x++) { if ($x * $x + $y * $y -le $r * $r + $r) { Plot ($cx + $x) ($cy + $y) $c } }
    }
}

# ---------------------------------------------------------------- the picture

# The night sky is dim blue, and everything drawn against it is dim too, so
# no cell in the sky needs the bright flag and none flips to bright blue.
$SKY = $BLUE
Fill-Cells 0 6 32 16 $SKY

# Logo on black: 'Kai' and 'o' glow from white down through fire, 'ZX'
# runs down the Spectrum's rainbow
$fire = @(($WHITE + $B), ($YELLOW + $B), ($YELLOW + $B), ($RED + $B), $RED)
Draw-Text 'Kai' 4 0 $fire 4 5
Draw-Text 'ZX' 16 0 @(($RED + $B), ($YELLOW + $B), ($GREEN + $B), ($CYAN + $B), ($BLUE + $B)) 4 5
Draw-Text 'o' 24 0 $fire 4 5

# Full moon with seas and two bats as holes of sky, so its cells only ever
# hold white and blue
$moonX = 188; $moonY = 82
Fill-Circle $moonX $moonY 30 $WHITE
foreach ($sea in @(@(-16, 6, 6), @(12, -12, 6), @(6, 8, 5), @(-8, 20, 4), @(18, 4, 3))) {
    Fill-Circle ($moonX + $sea[0]) ($moonY + $sea[1]) $sea[2] $SKY
}
Draw-Sprite 'flyer_a' 172 64 $SKY
Draw-Sprite 'flyer_b' 190 92 $SKY

# A bat swooping over the tower, black on the sky
Draw-Sprite 'flyer_b' 16 64 $BLACK

# Stars, only in cells that are still plain sky
$rng = New-Object Random 1982
for ($n = 0; $n -lt 60; $n++) {
    $x = $rng.Next(256); $y = $rng.Next(48, 120)
    $plain = $true
    for ($py = $y -band 0xF8; $py -lt ($y -band 0xF8) + 8; $py++) {
        for ($px = $x -band 0xF8; $px -lt ($x -band 0xF8) + 8; $px++) { if ((Get-Px $px $py) -ne $SKY) { $plain = $false } }
    }
    if ($plain) { Plot $x $y $(if ($rng.Next(3) -eq 0) { $CYAN } else { $WHITE }) }
}

# The far castle on its hill, black against the sky, with lit windows that
# sit wholly inside black cells
for ($x = 88; $x -lt 256; $x++) {
    $top = [int](150 - 10 * [Math]::Sin(($x - 88) / 40.0))
    for ($y = $top; $y -lt 176; $y++) { Plot $x $y $BLACK }
}
Fill-Rect 200 120 40 40 $BLACK                         # keep
for ($x = 200; $x -lt 240; $x += 8) { Fill-Rect $x 116 5 4 $BLACK }
Fill-Rect 144 128 16 32 $BLACK                         # watchtower
for ($y = 120; $y -lt 128; $y++) {                     # its pointed roof
    $h = [int](($y - 120) * 9 / 8)
    Fill-Rect (152 - $h) $y ($h * 2) 1 $BLACK
}
foreach ($w in @(@(209, 130), @(225, 130), @(217, 146), @(150, 138))) {
    Fill-Rect $w[0] $w[1] 3 4 $YELLOW
    Fill-Rect ($w[0] + 1) ($w[1] - 1) 1 1 $YELLOW
}

# The near tower, built from the game's brick tiles, with an arched window
Draw-Map 1 11 @('#.#.#.#')
Draw-Map 1 12 @(
    '#######',
    '#######',
    '#######',
    '#######',
    '#######',
    '#######',
    '#######',
    '#######',
    '#######'
)
foreach ($win in @(@(4, 14), @(4, 17))) {
    Fill-Cells $win[0] $win[1] 1 2 $BLACK
    Fill-Rect ($win[0] * 8 + 2) ($win[1] * 8 + 3) 4 11 ($YELLOW + $B)
    Fill-Rect ($win[0] * 8 + 3) ($win[1] * 8 + 2) 2 1 ($YELLOW + $B)
}

# Our hero, twice life size, leaping from the battlements toward the moon
Draw-Sprite 'jump' 72 56 $WHITE 2

# The ground: castle wall along the bottom, with spikes at the tower's foot
Draw-Map 0 21 @('################################')
Draw-Map 8 20 @('^^^^^^^^^^')

# Credits on black
Draw-Centred ([string][char]127 + ' 2026 Chris Trewartha') 23 ($WHITE + $B)

# ---------------------------------------------------------------- attributes

# For each cell, the commonest colour becomes paper and the next ink. Any
# other colour takes whichever of the two is nearer.
function Get-Rgb($c) { $p = $palette[$c]; @((($p -shr 16) -band 255), (($p -shr 8) -band 255), ($p -band 255)) }
function Get-Dist($a, $b) {
    $p = Get-Rgb $a; $q = Get-Rgb $b
    ($p[0] - $q[0]) * ($p[0] - $q[0]) + ($p[1] - $q[1]) * ($p[1] - $q[1]) + ($p[2] - $q[2]) * ($p[2] - $q[2])
}

$pix = New-Object bool[] (256 * 192)
$attr = New-Object byte[] 768
$clashes = @()
for ($cy = 0; $cy -lt 24; $cy++) {
    for ($cx = 0; $cx -lt 32; $cx++) {
        $count = @{}
        for ($y = $cy * 8; $y -lt $cy * 8 + 8; $y++) {
            for ($x = $cx * 8; $x -lt $cx * 8 + 8; $x++) {
                $c = Get-Px $x $y
                if ($c -eq $B) { $c = $BLACK }
                $count[$c] = 1 + $(if ($count.ContainsKey($c)) { $count[$c] } else { 0 })
            }
        }
        $ranked = @($count.GetEnumerator() | Sort-Object -Property @{ Expression = 'Value'; Descending = $true }, Name | ForEach-Object { [int]$_.Name })
        $paper = $ranked[0]
        $ink = if ($ranked.Count -gt 1) { $ranked[1] } else { $paper }
        if ($ranked.Count -gt 2) { $clashes += "($cx,$cy)" }
        $bright = ($paper -ge $B) -or ($ink -ge $B)
        $dim = (($paper -ne $BLACK) -and ($paper -lt $B)) -or (($ink -ne $BLACK) -and ($ink -lt $B))
        if ($bright -and $dim) { $clashes += "($cx,$cy)" }
        $attr[$cy * 32 + $cx] = ($ink -band 7) -bor (($paper -band 7) -shl 3) -bor $(if ($bright) { 0x40 } else { 0 })

        for ($y = $cy * 8; $y -lt $cy * 8 + 8; $y++) {
            for ($x = $cx * 8; $x -lt $cx * 8 + 8; $x++) {
                $c = Get-Px $x $y
                if ($c -eq $B) { $c = $BLACK }
                $on = if ($c -eq $ink) { $true } elseif ($c -eq $paper) { $false } else { (Get-Dist $c $ink) -lt (Get-Dist $c $paper) }
                $pix[$y * 256 + $x] = $on -and $ink -ne $paper
            }
        }
    }
}
if ($clashes) { Write-Warning "Colour clash in cells $($clashes -join ' ')" }

# ---------------------------------------------------------------- output

# Screen memory order: thirds, then pixel line within the cell, then cell row
function Write-Scr($path) {
    $scr = New-Object byte[] 6912
    for ($y = 0; $y -lt 192; $y++) {
        $base = (($y -band 0xC0) -shl 5) -bor (($y -band 7) -shl 8) -bor (($y -band 0x38) -shl 2)
        for ($cx = 0; $cx -lt 32; $cx++) {
            $v = 0
            for ($b = 0; $b -lt 8; $b++) { if ($pix[$y * 256 + $cx * 8 + $b]) { $v = $v -bor (0x80 -shr $b) } }
            $scr[$base + $cx] = $v
        }
    }
    [Array]::Copy($attr, 0, $scr, 6144, 768)
    [IO.File]::WriteAllBytes($path, $scr)
    Write-Host "Wrote $path"
}

function Write-Png($path) {
    $border = 32
    $colours = $palette | ForEach-Object { [Drawing.Color]::FromArgb(255, ($_ -shr 16) -band 255, ($_ -shr 8) -band 255, $_ -band 255) }

    $w = 256 + 2 * $border; $h = 192 + 2 * $border
    $native = New-Object Drawing.Bitmap $w, $h
    $g = [Drawing.Graphics]::FromImage($native)
    $g.Clear($colours[0])
    for ($y = 0; $y -lt 192; $y++) {
        for ($x = 0; $x -lt 256; $x++) {
            $a = $attr[($y -shr 3) * 32 + ($x -shr 3)]
            $bright = if ($a -band 0x40) { 8 } else { 0 }
            $c = if ($pix[$y * 256 + $x]) { $a -band 7 } else { ($a -shr 3) -band 7 }
            $native.SetPixel($x + $border, $y + $border, $colours[$c + $bright])
        }
    }
    $g.Dispose()

    $big = New-Object Drawing.Bitmap ($w * $Scale), ($h * $Scale)
    $g = [Drawing.Graphics]::FromImage($big)
    $g.InterpolationMode = 'NearestNeighbor'
    $g.PixelOffsetMode = 'Half'
    $g.DrawImage($native, 0, 0, $w * $Scale, $h * $Scale)
    $g.Dispose()

    New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
    $big.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    Write-Host "Wrote $path"
}

Write-Scr $scrOut
Write-Png $pngOut
