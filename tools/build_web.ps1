# Puts together the web page that plays the game in the browser:
#
#   web/index.html   page template
#   web/jsspeccy/    the JSSpeccy 3 emulator (GPL v3, see its COPYING)
#   build/game.tap   the game, from the main build
#   docs/*.png       loading screen and cover art
#
# Writes build/web/, a folder that any static web server can serve as is.
#
#   tools/build_web.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build/web'

$tap = Join-Path $root 'build/game.tap'
if (-not (Test-Path $tap)) { throw "build/game.tap not found: build the game first" }

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory $out | Out-Null
Copy-Item -Recurse (Join-Path $root 'web/jsspeccy') $out
Copy-Item (Join-Path $root 'docs/loading.png'), (Join-Path $root 'docs/cover.png') $out

# ---------------------------------------------------------------- icons

# The hero mid-jump, bright white on black, in front of the Spectrum's
# rainbow stripes. Drawn from data/sprites.txt, so it matches the game.
Add-Type -AssemblyName System.Drawing
$hero = @()
$inJump = $false
foreach ($line in [IO.File]::ReadAllLines((Join-Path $root 'data/sprites.txt'))) {
    if ($line -match '^\[(\w+)\]') { $inJump = $Matches[1] -eq 'jump' }
    elseif ($inJump -and $line.Length -ge 16 -and -not $line.StartsWith(';')) { $hero += $line.Substring(0, 16) }
}

function New-Icon($size) {
    $bmp = New-Object Drawing.Bitmap $size, $size
    $stripes = @('#FF0000', '#FFFF00', '#00FF00', '#00FFFF') | ForEach-Object { [Drawing.ColorTranslator]::FromHtml($_) }
    $w = $size * 0.12                       # stripe width, along the diagonal
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $k = [Math]::Floor(($x + $y - $size * 1.5) / $w)
            $bmp.SetPixel($x, $y, $(if ($k -ge 0 -and $k -lt 4) { $stripes[$k] } else { [Drawing.Color]::Black }))
        }
    }
    $scale = [Math]::Floor($size / 16)
    $off = [Math]::Floor(($size - 16 * $scale) / 2)
    for ($r = 0; $r -lt 16; $r++) {
        for ($c = 0; $c -lt 16; $c++) {
            if ($hero[$r][$c] -ne '#') { continue }
            for ($i = 0; $i -lt $scale * $scale; $i++) {
                $bmp.SetPixel($off + $c * $scale + $i % $scale, $off + $r * $scale + [Math]::Floor($i / $scale), [Drawing.Color]::White)
            }
        }
    }
    $mem = New-Object IO.MemoryStream
    $bmp.Save($mem, [Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $mem.ToArray()
}

$png32 = New-Icon 32
[IO.File]::WriteAllBytes((Join-Path $out 'favicon.png'), $png32)
[IO.File]::WriteAllBytes((Join-Path $out 'apple-touch-icon.png'), (New-Icon 180))

# favicon.ico, for browsers and tools that only look there: one 32x32 PNG
# in an icon directory
$ico = New-Object IO.BinaryWriter ([IO.File]::Create((Join-Path $out 'favicon.ico')))
$ico.Write([UInt16]0); $ico.Write([UInt16]1); $ico.Write([UInt16]1)          # reserved, type icon, 1 image
$ico.Write([byte]32); $ico.Write([byte]32); $ico.Write([UInt16]0)            # 32x32, no palette
$ico.Write([UInt16]1); $ico.Write([UInt16]32)                                # planes, bits per pixel
$ico.Write([UInt32]$png32.Length); $ico.Write([UInt32]22)                    # image size, offset
$ico.Write($png32)
$ico.Close()

# The tape's name carries a hash of it, so browsers and caches never hand
# out an old build. It goes in the name rather than a ?v= query, because
# JSSpeccy tells the file type from the end of the URL, and hangs on a
# query string.
$hash = (Get-FileHash $tap -Algorithm SHA256).Hash.Substring(0, 12).ToLower()
$tapName = "kaizxo-$hash.tap"
Copy-Item $tap (Join-Path $out $tapName)
$html = [IO.File]::ReadAllText((Join-Path $root 'web/index.html'))
$html = $html.Replace('{{TAP}}', $tapName)
[IO.File]::WriteAllText((Join-Path $out 'index.html'), $html)

Write-Host "Wrote $out ($tapName)"
