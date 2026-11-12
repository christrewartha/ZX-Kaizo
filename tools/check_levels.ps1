# Proves every level in data/world.txt can be finished, by searching the
# inputs frame by frame through a port of the player physics
# (tools/level_sim.cs), with the enemies on their patrols. Each level is
# solved twice: straight to the exit, and by way of its trophy.
#
# For each route it reports:
# - the time it takes (the fewest frames possible if there are no
#   enemies; with enemies the search is looser, see LevelSim.Solve)
# - the largest hold that still finishes: each input held for that many
#   frames before it may change. Only a rough guide, as a route can get
#   lucky with where its blocks of frames fall.
# - the tightest jump: of the 9 ways to press each jump a frame early,
#   on time or late, and let go a frame early, on time or late, how many
#   still get through (LevelSim.Tolerance). Best over the routes found.
#   Under 4/9 is close to frame-perfect: fine for a trophy, not for the
#   way to the exit.
#
#   powershell -File tools/check_levels.ps1 [-Level <n>] [-MaxHold <n>] [-Quick] [-Amiga]
#
# -Quick skips the tightest-jump measure, which takes most of the time.
# -Amiga uses the Amiga's shorter boxes for Foxy (PLAYER_TOP and
# PLAYER_HURT_TOP in tools/build_amiga.ps1).

param([int]$Level = 0, [int]$MaxHold = 6, [int]$MaxStates = 12000000, [switch]$Quick, [switch]$Amiga)

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
if ($Amiga) { [LevelSim]::TOP = 2; [LevelSim]::HURT_TOP = 6 }

$file = 'data/world.txt'
$levels = Read-World $root $file
$failed = 0

# Solves one route at every hold; returns a summary, or $null if it can't
# be finished
function Measure-Route($sim, $lv, [bool]$trophy) {
    $frames = @()
    $tightest = -1; $where = ''
    $explored = 0; $largest = 0
    # A search can lose its way at one hold and find a route at the next,
    # so every hold is tried
    for ($hold = 1; $hold -le $MaxHold; $hold++) {
        $f = $sim.Solve($lv.Start[0], $lv.Start[1], $hold, $MaxStates, $trophy, [ref]$explored)
        if ($f -lt 0) { continue }
        $frames += $f
        $largest = $hold
        if ($Quick -or $hold -gt 4) { continue }
        $worst = $sim.Tolerance($lv.Start[0], $lv.Start[1], $sim.LastPath, $trophy)
        if ($worst -gt $tightest) {
            $tightest = $worst; $where = ''
            for ($j = 0; $j -lt $sim.JumpScores.Length; $j++) {
                if ($sim.JumpScores[$j] -eq $worst) { $where = " at x=$($sim.JumpX[$j]) y=$($sim.JumpY[$j])"; break }
            }
        }
    }
    if ($frames.Count -eq 0) {
        return @{ Ok = $false; Why = $(if ($explored -gt $MaxStates) { "gave up after $MaxStates states" } else { 'unreachable' }) }
    }
    $text = '{0,5:0.0}s hold {1}' -f ($frames[0] / 50), $largest
    if (-not $Quick) { $text += "  tightest $tightest/9$where" }
    return @{ Ok = $true; Text = $text }
}

foreach ($lv in $levels) {
    if ($Level -and $lv.Index + 1 -ne $Level) { continue }

    # Two empty rows below the room, like room_coll
    $coll = New-Object byte[] (($roomRows + 2) * $roomCols)
    for ($i = 0; $i -lt $lv.Cells.Count; $i++) { $coll[$i] = $tileFlags[$lv.Cells[$i]] }
    $sim = New-Object LevelSim (, $coll)
    foreach ($e in $lv.EnemyDefs) { $sim.AddEnemy($e[0], $e[1], $e[2], $e[3], $e[4], $e[5]) }

    $label = '{0,2} {1,-22}' -f ($lv.Index + 1), $lv.Name
    $exit = Measure-Route $sim $lv $false
    if (-not $exit.Ok) {
        Warn $file $lv.Line "$($lv.Name): cannot be finished ($($exit.Why))"
        $failed++
        continue
    }
    Write-Host "$label exit   $($exit.Text)"

    if (-not $lv.Trophy) {
        Warn $file $lv.Line "$($lv.Name): no trophy (T)"
        continue
    }
    $cup = Measure-Route $sim $lv $true
    if (-not $cup.Ok) {
        Warn $file $lv.Line "$($lv.Name): the trophy can't be won ($($cup.Why))"
        $failed++
        continue
    }
    Write-Host ('{0,-25} trophy {1}' -f '', $cup.Text)
}
if ($failed) { exit 1 }
