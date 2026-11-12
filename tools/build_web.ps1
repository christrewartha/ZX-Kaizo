# Puts together the web page that plays the game in the browser:
#
#   web/index.html   page template
#   web/jsspeccy/    the JSSpeccy 3 emulator (GPL v3, see its COPYING)
#   build/game.tap   the game, from the main build
#   docs/*.png       loading screen and cover art
#   web/c64.html     the C64 version's page, at c64/
#   web/tiny8bit/    the Tiny Emulators C64 (see its LICENSE.txt)
#   build/kaiz64o.*  the C64 version, from tools/build_c64.ps1
#   web/amiga.html   the Amiga version's page, at amiga/, with vAmigaWeb
#                    (downloaded into build/cache) and build/amiga/kaiz.adf
#
# Writes build/web/, a folder that any static web server can serve as is.
#
#   tools/build_web.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build/web'

$tap = Join-Path $root 'build/game.tap'
if (-not (Test-Path $tap)) { throw "build/game.tap not found: build the game first" }
$prg = Join-Path $root 'build/kaiz64o.prg'
$d64 = Join-Path $root 'build/kaiz64o.d64'
if (-not (Test-Path $d64)) { throw "build/kaiz64o.d64 not found: run tools/build_c64.ps1 first" }
$adf = Join-Path $root 'build/amiga/kaiz.adf'
if (-not (Test-Path $adf)) { throw "build/amiga/kaiz.adf not found: run tools/build_amiga.ps1 first" }

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

# --------------------------------------------------------- C64 version

# The same hash-in-the-name trick for the program and disk image. The
# emulator frame loads the program; the page offers the disk image.
$c64 = Join-Path $out 'c64'
New-Item -ItemType Directory $c64 | Out-Null
Copy-Item -Recurse (Join-Path $root 'web/tiny8bit') $c64
Copy-Item (Join-Path $root 'docs/kaiz64o.png') $c64
$hash = (Get-FileHash $prg -Algorithm SHA256).Hash.Substring(0, 12).ToLower()
$prgName = "kaiz64o-$hash.prg"
$d64Name = "kaiz64o-$hash.d64"
Copy-Item $prg (Join-Path $c64 $prgName)
Copy-Item $d64 (Join-Path $c64 $d64Name)
$html = [IO.File]::ReadAllText((Join-Path $root 'web/c64.html'))
$html = $html.Replace('{{PRG}}', $prgName).Replace('{{D64}}', $d64Name)
[IO.File]::WriteAllText((Join-Path $c64 'index.html'), $html)


# -------------------------------------------------------- Amiga version

# The Amiga runs on vAmigaWeb (GPL v3), with the free AROS ROM. It's big
# (the emulator alone is 8MB), so rather than keep a copy in the repository
# like JSSpeccy, it's downloaded once, at a fixed version, into build/cache.
# Only what the page needs goes on the site: not the documentation, not the
# other ROMs, and not the service worker (which would cache old builds and
# offer upgrades; without it vAmigaWeb just logs that it couldn't register).
$vamigaCommit = 'a83eb5361b078fe332ac6a5af3da17bf5fcb3c68'
$cache = Join-Path $root "build/cache/vamigaweb-$vamigaCommit"
if (-not (Test-Path (Join-Path $cache 'vAmiga.wasm'))) {
    Write-Host "Downloading vAmigaWeb ($vamigaCommit)..."
    $zip = Join-Path ([IO.Path]::GetTempPath()) "vamigaweb-$vamigaCommit.zip"
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest "https://github.com/vAmigaWeb/vAmigaWeb.github.io/archive/$vamigaCommit.zip" -OutFile $zip -UseBasicParsing
    $unpack = Join-Path ([IO.Path]::GetTempPath()) "vamigaweb-$vamigaCommit"
    if (Test-Path $unpack) { Remove-Item -Recurse -Force $unpack }
    Expand-Archive $zip $unpack
    New-Item -ItemType Directory -Force (Split-Path $cache) | Out-Null
    Move-Item (Join-Path $unpack "vAmigaWeb.github.io-$vamigaCommit") $cache
    Invoke-WebRequest 'https://raw.githubusercontent.com/vAmigaWeb/vAmigaWeb/main/LICENSE' -OutFile (Join-Path $cache 'LICENSE') -UseBasicParsing
    Remove-Item -Recurse -Force $unpack, $zip
}

$amiga = Join-Path $out 'amiga'
$emu = Join-Path $amiga 'vamiga'
New-Item -ItemType Directory -Force (Join-Path $emu 'roms') | Out-Null
foreach ($item in 'index.html', 'vAmiga.js', 'vAmiga.wasm', 'manifest.json', 'LICENSE', 'css', 'img', 'js', 'sounds') {
    Copy-Item -Recurse (Join-Path $cache $item) $emu
}
Get-ChildItem (Join-Path $emu 'js') -Filter '*.map' | Remove-Item
foreach ($rom in 'aros-rom-20260820.bin', 'aros-ext-20260820.bin') {
    Copy-Item (Join-Path $cache "roms/$rom") (Join-Path $emu 'roms')
}
Copy-Item (Join-Path $root 'docs/kaizamigo.png') $amiga
$hash = (Get-FileHash $adf -Algorithm SHA256).Hash.Substring(0, 12).ToLower()
$adfName = "kaizamigo-$hash.adf"
Copy-Item $adf (Join-Path $amiga $adfName)
$html = [IO.File]::ReadAllText((Join-Path $root 'web/amiga.html'))
$html = $html.Replace('{{ADF}}', $adfName)
[IO.File]::WriteAllText((Join-Path $amiga 'index.html'), $html)

Write-Host "Wrote $out ($tapName, c64/$prgName, amiga/$adfName)"
