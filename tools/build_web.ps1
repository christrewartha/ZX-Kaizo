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
