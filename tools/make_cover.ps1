# Draws the README cover image as a genuine Spectrum screen: a 256x192
# bitmap with one ink and one paper per 8x8 cell, so it clashes exactly as
# the real machine would. Tiles come from src/room.c, sprites from
# data/sprites.txt and text from the 48K ROM's character set.
#
#   tools/make_cover.ps1 [-Rom <48.rom>] [-Out docs/cover.png] [-Scale 3]
#
# Also writes a .scr (6912-byte screen dump) beside the PNG, which can be
# loaded straight into an emulator.

param(
    [string]$Rom = 'C:/Program Files (x86)/Fuse/roms/48.rom',
    [string]$Out,
    [int]$Scale = 3
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $root 'docs/cover.png' }

# Attribute colours (bright black is still black)
$BLUE = 1; $RED = 2; $MAGENTA = 3; $GREEN = 4; $CYAN = 5; $YELLOW = 6; $WHITE = 7
function Attr($ink, $paper = 0, [switch]$Bright) { $ink -bor ($paper -shl 3) -bor ($(if ($Bright) { 0x40 } else { 0 })) }

$pix = New-Object bool[] (256 * 192)
$attr = New-Object byte[] 768
$used = New-Object bool[] 768

function Set-Cell($cx, $cy, $a) {
    if ($cx -lt 0 -or $cx -gt 31 -or $cy -lt 0 -or $cy -gt 23) { return }
    $attr[$cy * 32 + $cx] = $a
    $used[$cy * 32 + $cx] = $true
}

function Set-Pixel($x, $y, $on = $true) {
    if ($x -ge 0 -and $x -lt 256 -and $y -ge 0 -and $y -lt 192) { $pix[$y * 256 + $x] = $on }
}

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

function Draw-Tile($cx, $cy, $ch) {
    $id = $T[[string]$ch]
    for ($l = 0; $l -lt 8; $l++) {
        for ($b = 0; $b -lt 8; $b++) {
            Set-Pixel ($cx * 8 + $b) ($cy * 8 + $l) ((($tiles[$id][$l] -shr (7 - $b)) -band 1) -eq 1)
        }
    }
    Set-Cell $cx $cy $tileAttr[$id]
}

# Rows of map characters, top-left at cell (cx, cy); '.' leaves a cell alone
function Draw-Map($cx, $cy, [string[]]$rows) {
    for ($r = 0; $r -lt $rows.Count; $r++) {
        for ($c = 0; $c -lt $rows[$r].Length; $c++) {
            if ($rows[$r][$c] -ne '.') { Draw-Tile ($cx + $c) ($cy + $r) $rows[$r][$c] }
        }
    }
}

# 16x16 sprite on whole cells, so it owns its 2x2 attributes
function Draw-Sprite($art, $cx, $cy, $a, [switch]$Mirror) {
    $rows = $sprites[$art]
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $sx = if ($Mirror) { 15 - $x } else { $x }
            if ($rows[$y][$sx] -eq '#') { Set-Pixel ($cx * 8 + $x) ($cy * 8 + $y) }
        }
    }
    for ($i = 0; $i -lt 4; $i++) { Set-Cell ($cx + ($i -band 1)) ($cy + ($i -shr 1)) $a }
}

# ROM font text. Scaled text drops the last pixel line of every glyph row
# for a scanline stripe. $attrs gives one attribute per cell row.
function Draw-Text([string]$text, $cx, $cy, $attrs, $scale = 1) {
    $attrs = @($attrs)
    for ($i = 0; $i -lt $text.Length; $i++) {
        $g = ([int][char]$text[$i] - 32) * 8
        $ox = ($cx + $i * $scale) * 8
        for ($l = 0; $l -lt 8; $l++) {
            for ($b = 0; $b -lt 8; $b++) {
                if ((($font[$g + $l] -shr (7 - $b)) -band 1) -eq 0) { continue }
                for ($sy = 0; $sy -lt $scale; $sy++) {
                    if ($scale -gt 1 -and $sy -eq $scale - 1) { continue }
                    for ($sx = 0; $sx -lt $scale; $sx++) {
                        Set-Pixel ($ox + $b * $scale + $sx) ($cy * 8 + $l * $scale + $sy)
                    }
                }
            }
        }
        for ($r = 0; $r -lt $scale; $r++) {
            for ($c = 0; $c -lt $scale; $c++) {
                Set-Cell ($cx + $i * $scale + $c) ($cy + $r) $attrs[[Math]::Min($r, $attrs.Count - 1)]
            }
        }
    }
}

function Draw-Centred([string]$text, $cy, $a) {
    Draw-Text $text ([int][Math]::Floor((32 - $text.Length) / 2)) $cy $a
}

# ---------------------------------------------------------------- the screen

# Title across the full width, cooling from yellow to red down the rows
Draw-Text 'ZX-KAIZO' 0 1 @((Attr $YELLOW -Bright), (Attr $YELLOW -Bright), (Attr $RED -Bright), (Attr $RED)) 4
Draw-Centred 'A PLATFORMER WITH NO MERCY' 6 (Attr $CYAN -Bright)

# A corner of the castle at night
$moonX = 228; $moonY = 76
for ($y = -11; $y -le 11; $y++) {
    for ($x = -11; $x -le 11; $x++) {
        $lit = ($x * $x + $y * $y) -le 110
        $bite = (($x - 5) * ($x - 5) + ($y + 2) * ($y + 2)) -le 80
        if ($lit -and -not $bite) { Set-Pixel ($moonX + $x) ($moonY + $y) }
    }
}
for ($cy = 8; $cy -le 11; $cy++) { for ($cx = 26; $cx -le 29; $cx++) { Set-Cell $cx $cy (Attr $YELLOW -Bright) } }

Draw-Map 0 8 @(
    '#.#.#',
    '#####',
    '#####',
    '####.',
    '####.',
    '####.',
    '####.',
    '####.',
    '####.',
    '####.',
    '####.',
    '#####',
    '#####'
)
Draw-Map 8 13 @('=====')
Draw-Map 15 11 @('XX')
Draw-Map 15 12 @('XX')
Draw-Map 15 13 @('vv')
Draw-Map 22 15 @('=====')
Draw-Map 5 19 @(
    '#######    ################',
    '#######^^^^################'
)

Draw-Sprite 'jump' 12 15 (Attr $WHITE -Bright)
Draw-Sprite 'walker_a' 18 17 (Attr $GREEN -Bright)
Draw-Sprite 'flyer_a' 20 9 (Attr $MAGENTA -Bright)
Draw-Sprite 'flyer_b' 9 10 (Attr $CYAN -Bright)

# Bottom bar, as in the game's HUD
for ($cx = 0; $cx -lt 32; $cx++) { Set-Cell $cx 22 (Attr $YELLOW $BLUE -Bright) }
Draw-Centred 'The Belfry' 22 (Attr $YELLOW $BLUE -Bright)
Draw-Text ([string][char]127 + ' 2026 Chris Trewartha') 1 23 (Attr $WHITE)

# The rainbow stripes, on cell diagonals so every cell holds just two colours
$stripes = @($RED, $YELLOW, $GREEN, $CYAN)
$s0 = 52                         # first stripe's diagonal, in cells (x + y)
for ($cy = 0; $cy -lt 24; $cy++) {
    for ($cx = 0; $cx -lt 32; $cx++) {
        $k = $cx + $cy - $s0       # pixels with px + py < 8 are stripe k, the rest k + 1
        if ($k -lt -1 -or $k -gt 3) { continue }
        $a = if ($k -ge 0) { $stripes[$k] } else { 0 }
        $b = if ($k -lt 3) { $stripes[$k + 1] } else { 0 }
        for ($py = 0; $py -lt 8; $py++) {
            for ($px = 0; $px -lt 8; $px++) { Set-Pixel ($cx * 8 + $px) ($cy * 8 + $py) (($px + $py) -lt 8) }
        }
        Set-Cell $cx $cy ((Attr $a $b -Bright))
    }
}

# Stars in the empty sky
$rng = New-Object Random 1982
for ($n = 0; $n -lt 40; $n++) {
    $x = $rng.Next(256); $y = $rng.Next(8, 120)
    $c = ($y -shr 3) * 32 + ($x -shr 3)
    if ($used[$c]) { continue }
    Set-Pixel $x $y
    Set-Cell ($x -shr 3) ($y -shr 3) $(if ($rng.Next(3) -eq 0) { Attr $CYAN } else { Attr $WHITE })
}

# ---------------------------------------------------------------- output

$border = 32
$palette = @(0x000000, 0x0000D7, 0xD70000, 0xD700D7, 0x00D700, 0x00D7D7, 0xD7D700, 0xD7D7D7,
             0x000000, 0x0000FF, 0xFF0000, 0xFF00FF, 0x00FF00, 0x00FFFF, 0xFFFF00, 0xFFFFFF)
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

New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
$big.Save($Out, [Drawing.Imaging.ImageFormat]::Png)

# Screen memory order: thirds, then pixel line within the cell, then cell row
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
[IO.File]::WriteAllBytes([IO.Path]::ChangeExtension($Out, '.scr'), $scr)
Write-Host "Wrote $Out"
