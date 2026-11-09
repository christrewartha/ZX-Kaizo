# Proves every level in data/world.txt can be finished, by searching the
# inputs frame by frame through a port of the player physics
# (tools/level_sim.cs). Enemies are ignored, so they can only make a level
# harder than reported.
#
# For each level it reports the fewest frames to the exit, then the same
# with each input held for 2, 3, 4... frames before it may change. A level
# only finishable at hold 1 needs frame-perfect play; the larger the hold
# that still finishes, the more forgiving the level.
#
#   powershell -File tools/check_levels.ps1 [-Level <n>] [-MaxHold <n>]

param([int]$Level = 0, [int]$MaxHold = 6, [int]$MaxStates = 12000000)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Fail($file, $line, $msg) {
    [Console]::Error.WriteLine("${file}:${line}:1: error: $msg")
    exit 1
}
function Warn($file, $line, $msg) {
    [Console]::Error.WriteLine("${file}:${line}:1: warning: $msg")
}

. (Join-Path $PSScriptRoot 'world.ps1')
Add-Type -Path (Join-Path $PSScriptRoot 'level_sim.cs')

$file = 'data/world.txt'
$levels = Read-World $root $file
$failed = 0

foreach ($lv in $levels) {
    if ($Level -and $lv.Index + 1 -ne $Level) { continue }

    # Two empty rows below the room, like room_coll
    $coll = New-Object byte[] (($roomRows + 2) * $roomCols)
    for ($i = 0; $i -lt $lv.Cells.Count; $i++) { $coll[$i] = $tileFlags[$lv.Cells[$i]] }
    $sim = New-Object LevelSim (, $coll)

    $results = @()
    $explored = 0
    for ($hold = 1; $hold -le $MaxHold; $hold++) {
        $frames = $sim.Solve($lv.Start[0], $lv.Start[1], $hold, $MaxStates, [ref]$explored)
        if ($frames -lt 0) { break }
        $results += $frames
    }

    $label = '{0,2} {1,-24}' -f ($lv.Index + 1), $lv.Name
    if ($results.Count -eq 0) {
        $why = if ($explored -gt $MaxStates) { "gave up after $MaxStates states" } else { 'exit unreachable' }
        Warn $file $lv.Line "$($lv.Name): cannot be finished ($why)"
        $failed++
    } else {
        $secs = '{0:0.0}s' -f ($results[0] / 50)
        Write-Host "$label $secs   finishes up to hold $($results.Count)   (frames: $($results -join ' '))"
    }
}
if ($failed) { exit 1 }
