# Reads data/world.txt into a list of levels. Dot-sourced by build_data.ps1
# and check_levels.ps1, which must define Fail and Warn first.

$roomCols = 32
$roomRows = 22                  # must match ROOM_ROWS in src/room.h
$maxY = $roomRows * 8 - 16      # lowest sprite y inside a room

# Must match the T_* tile ids and tile_coll in src/room.c
$tileIds = @{ [char]' ' = 0; [char]'#' = 1; [char]'X' = 2; [char]'=' = 3; [char]'^' = 4; [char]'v' = 5; [char]'E' = 6 }
$tileFlags = @(0, 1, 1, 2, 4, 4, 8)
$T_EXIT = 6

$colours = @{ blue = 0x41; red = 0x42; magenta = 0x43; green = 0x44; cyan = 0x45; yellow = 0x46; white = 0x47 }
$enemyTypes = @{ walker = 'ENEMY_WALKER'; flyer = 'ENEMY_FLYER' }
$maxEnemies = 4

function Read-World($root, $file) {
    $lines = [IO.File]::ReadAllLines((Join-Path $root $file))
    $levels = New-Object Collections.Generic.List[object]
    $i = 0
    while ($i -lt $lines.Count) {
        $line = $lines[$i].Trim()
        $lineNo = $i + 1
        $i++
        if ($line -eq '' -or $line.StartsWith(';')) { continue }
        if ($line -notmatch '^level\s+(.+)$') { Fail $file $lineNo "expected 'level <name>'" }

        $name = $Matches[1].Trim()
        if ($name.Length -gt 32) { Fail $file $lineNo 'level name longer than 32 characters' }
        $level = @{ Name = $name; Line = $lineNo; Index = $levels.Count
                    Enemies = New-Object Collections.Generic.List[string]
                    Cells = New-Object int[] ($roomCols * $roomRows); Start = $null; Exits = 0 }
        $levels.Add($level)

        # Enemies, then exactly $roomRows lines of map
        while ($i -lt $lines.Count -and $lines[$i].Trim() -match '^enemy\s+(.*)$') {
            $lineNo = $i + 1
            $i++
            $f = $Matches[1] -split '\s+'
            if ($f.Count -ne 7) { Fail $file $lineNo 'enemy <type> <x> <y> <min> <max> <speed> <colour>' }
            if (-not $enemyTypes.ContainsKey($f[0])) { Fail $file $lineNo "unknown enemy type '$($f[0])'" }
            if (-not $colours.ContainsKey($f[6])) { Fail $file $lineNo "unknown colour '$($f[6])'" }
            $x = [int]$f[1]; $y = [int]$f[2]; $min = [int]$f[3]; $max = [int]$f[4]
            $speed = [int][Math]::Round([double]::Parse($f[5], [Globalization.CultureInfo]::InvariantCulture) * 256)
            $limit = if ($f[0] -eq 'walker') { 239 } else { $maxY }
            if ($x -gt 239 -or $y -gt $maxY) { Fail $file $lineNo "enemy position outside the room (x <= 239, y <= $maxY)" }
            if ($min -lt 1 -or $max -gt $limit -or $min -gt $max) { Fail $file $lineNo "patrol limits must satisfy 1 <= min <= max <= $limit" }
            if ($speed -eq 0) { Fail $file $lineNo 'enemy speed is zero' }
            if ($level.Enemies.Count -eq $maxEnemies) { Fail $file $lineNo "more than $maxEnemies enemies in one level" }
            $level.Enemies.Add("    { $($enemyTypes[$f[0]]), $x, $y, $min, $max, $speed, 0x$('{0:X2}' -f $colours[$f[6]]) },")
        }

        if ($i + $roomRows -gt $lines.Count) { Fail $file $lines.Count "$name needs $roomRows rows of map" }
        for ($r = 0; $r -lt $roomRows; $r++, $i++) {
            $lineNo = $i + 1
            $text = $lines[$i]
            if ($text.Length -gt $roomCols) { Fail $file $lineNo "map row is $($text.Length) cells wide, max $roomCols" }
            $text = $text.PadRight($roomCols)
            for ($c = 0; $c -lt $roomCols; $c++) {
                $ch = $text[$c]
                if ($ch -eq [char]'S') {
                    if ($level.Start) { Fail $file $lineNo "more than one S in $name" }
                    if ($r -eq 0) { Fail $file $lineNo 'S must be below the top row' }
                    $level.Start = @(($c * 8), ($r * 8 - 8))
                    $ch = [char]' '
                }
                if (-not $tileIds.ContainsKey($ch)) { Fail $file $lineNo "unknown tile '$ch' in $name column $c" }
                $t = $tileIds[$ch]
                if ($t -eq $T_EXIT) { $level.Exits++ }
                $level.Cells[$r * $roomCols + $c] = $t
            }
        }
        if (-not $level.Start) { Fail $file $level.Line "no S (player start) in $name" }
        if ($level.Exits -eq 0) { Fail $file $level.Line "no E (exit) in $name" }
    }
    if ($levels.Count -eq 0) { Fail $file 1 'no levels' }
    if ($levels.Count -gt 99) { Fail $file 1 'more than 99 levels' }
    return ,$levels
}
