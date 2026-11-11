# Draws a level with the route the solver finds through it: each cell the
# middle of the player passes through is marked with a '.', and each jump
# with a letter (a, b, c...) where it was pressed. Then lists the jumps,
# with how forgiving each one is (see LevelSim.Tolerance).
#
#   powershell -File tools/show_route.ps1 -Level <n> [-Trophy] [-Hold <n>]

param([Parameter(Mandatory)][int]$Level, [switch]$Trophy, [int]$Hold = 2, [int]$MaxStates = 12000000)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
function Fail($file, $line, $msg) { [Console]::Error.WriteLine("${file}:${line}:1: error: $msg"); exit 1 }
function Warn($file, $line, $msg) { [Console]::Error.WriteLine("${file}:${line}:1: warning: $msg") }

. (Join-Path $PSScriptRoot 'world.ps1')
Add-Type -Path (Join-Path $PSScriptRoot 'level_sim.cs')
$lv = (Read-World $root 'data/world.txt')[$Level - 1]

$coll = New-Object byte[] (($roomRows + 2) * $roomCols)
for ($i = 0; $i -lt $lv.Cells.Count; $i++) { $coll[$i] = $tileFlags[$lv.Cells[$i]] }
$sim = New-Object LevelSim (, $coll)
foreach ($e in $lv.EnemyDefs) { $sim.AddEnemy($e[0], $e[1], $e[2], $e[3], $e[4], $e[5]) }

$explored = 0
$frames = $sim.Solve($lv.Start[0], $lv.Start[1], $Hold, $MaxStates, [bool]$Trophy, [ref]$explored)
if ($frames -lt 0) { Write-Host "No route (explored $explored states)"; exit 1 }
$path = $sim.LastPath

$chars = ' #X=^vET'
$grid = @()
for ($r = 0; $r -lt $roomRows; $r++) {
    $row = ''
    for ($c = 0; $c -lt $roomCols; $c++) { $row += $chars[$lv.Cells[$r * $roomCols + $c]] }
    $grid += , $row.ToCharArray()
}

$null = $sim.Tolerance($lv.Start[0], $lv.Start[1], $path, [bool]$Trophy)
$p = [LevelSim]::Spawn($lv.Start[0], $lv.Start[1])
$jump = 0
for ($t = 1; $t -le $path.Length; $t++) {
    $null = $sim.Step([ref]$p, $path[$t - 1], $t)
    $cx = ((($p.x -shr 8) -band 255) + 8) -shr 3; $cy = ((($p.y -shr 8) -band 255) + 8) -shr 3
    if ($cy -lt $roomRows -and $cx -lt $roomCols -and $grid[$cy][$cx] -eq ' ') { $grid[$cy][$cx] = '.' }
    if ($jump -lt $sim.JumpFrames.Length -and $sim.JumpFrames[$jump] -eq $t) {
        if ($cy -lt $roomRows -and $cx -lt $roomCols) { $grid[$cy][$cx] = [char](97 + ($jump % 26)) }
        $jump++
    }
}
$what = if ($Trophy) { 'trophy route' } else { 'exit route' }
Write-Host "$($lv.Name), $what at hold ${Hold}: $frames frames"
Write-Host ('   ' + (-join (0..31 | % { $_ % 10 })))
for ($r = 0; $r -lt $roomRows; $r++) { Write-Host ('{0,2} {1}' -f $r, (-join $grid[$r])) }
for ($j = 0; $j -lt $sim.JumpFrames.Length; $j++) {
    Write-Host ('  {0}: frame {1,4} at x={2,3} y={3,3}  {4}/9' -f [char](97 + ($j % 26)), $sim.JumpFrames[$j], $sim.JumpX[$j], $sim.JumpY[$j], $sim.JumpScores[$j])
}
