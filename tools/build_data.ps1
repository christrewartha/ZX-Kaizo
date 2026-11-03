# Converts the text sources in data/ into compact C data in build/gen/:
#   data/world.txt   -> rooms_data.c   (run-length encoded maps, room table)
#   data/sprites.txt -> sprite_art.c/h (16-bit rows, ART_* indices)
# Errors and warnings are reported as file:line: so VS Code's problem
# matcher picks them up.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$gen = Join-Path $root 'build/gen'
New-Item -ItemType Directory -Force $gen | Out-Null

function Fail($file, $line, $msg) {
    [Console]::Error.WriteLine("${file}:${line}:1: error: $msg")
    exit 1
}

function Warn($file, $line, $msg) {
    [Console]::Error.WriteLine("${file}:${line}:1: warning: $msg")
}

function Write-Gen($name, $text) {
    [IO.File]::WriteAllText((Join-Path $gen $name), $text)
}

# ---------------------------------------------------------------- rooms

$roomCols = 32
$roomRows = 22                  # must match ROOM_ROWS in src/room.h
$maxY = $roomRows * 8 - 16      # lowest sprite y inside a room

# Must match the T_* tile ids in src/room.c
$tileIds = @{ [char]' ' = 0; [char]'#' = 1; [char]'X' = 2; [char]'=' = 3; [char]'^' = 4; [char]'v' = 5 }
$solidTiles = @(1, 2)
$colours = @{ blue = 0x41; red = 0x42; magenta = 0x43; green = 0x44; cyan = 0x45; yellow = 0x46; white = 0x47 }
$enemyTypes = @{ walker = 'ENEMY_WALKER'; flyer = 'ENEMY_FLYER' }
$maxEnemies = 4

$file = 'data/world.txt'
$lines = [IO.File]::ReadAllLines((Join-Path $root $file))

# --- room declarations

$rooms = New-Object Collections.Generic.List[object]
$bySlot = @{}
$mapStart = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i].Trim()
    if ($line -eq '' -or $line.StartsWith(';')) { continue }
    if ($line -eq 'map:') { $mapStart = $i + 1; break }
    $lineNo = $i + 1
    if ($line -match '^room\s+(\d+)\s+(\d+)\s+(.+)$') {
        $gx = [int]$Matches[1]; $gy = [int]$Matches[2]; $name = $Matches[3].Trim()
        if ($bySlot.ContainsKey("$gx,$gy")) { Fail $file $lineNo "two rooms at $gx,$gy" }
        if ($name.Length -gt 32) { Fail $file $lineNo 'room name longer than 32 characters' }
        $room = @{ X = $gx; Y = $gy; Name = $name; Line = $lineNo; Index = $rooms.Count
                   Enemies = New-Object Collections.Generic.List[string] }
        $rooms.Add($room)
        $bySlot["$gx,$gy"] = $room
    } elseif ($line -match '^enemy\s+(.*)$') {
        if ($rooms.Count -eq 0) { Fail $file $lineNo 'enemy before any room' }
        $room = $rooms[$rooms.Count - 1]
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
        if ($room.Enemies.Count -eq $maxEnemies) { Fail $file $lineNo "more than $maxEnemies enemies in one room" }
        $room.Enemies.Add("    { $($enemyTypes[$f[0]]), $x, $y, $min, $max, $speed, 0x$('{0:X2}' -f $colours[$f[6]]) },")
    } else {
        Fail $file $lineNo "expected 'room', 'enemy' or 'map:'"
    }
}
if ($mapStart -lt 0) { Fail $file $lines.Count "missing 'map:'" }
if ($rooms.Count -eq 0) { Fail $file $mapStart 'no rooms declared' }
if ($rooms.Count -gt 254) { Fail $file $mapStart 'too many rooms' }

$gridW = ($rooms | ForEach-Object { $_.X } | Measure-Object -Maximum).Maximum + 1
$gridH = ($rooms | ForEach-Object { $_.Y } | Measure-Object -Maximum).Maximum + 1
$needLines = $gridH * $roomRows + ($gridH - 1)
if ($lines.Count - $mapStart -lt $needLines) {
    Fail $file $lines.Count "map needs $needLines lines for a ${gridW}x${gridH} grid"
}

# --- slice the grid into rooms

foreach ($room in $rooms) { $room.Cells = New-Object int[] ($roomCols * $roomRows); $room.Rows = New-Object int[] $roomRows }
$start = $null

for ($gy = 0; $gy -lt $gridH; $gy++) {
    $base = $mapStart + $gy * ($roomRows + 1)
    if ($gy -gt 0 -and -not $lines[$base - 1].StartsWith('-')) {
        Fail $file $base "expected a '-' separator line between grid rows"
    }
    for ($r = 0; $r -lt $roomRows; $r++) {
        $lineNo = $base + $r + 1
        $slots = $lines[$base + $r] -split '\|'
        if ($slots.Count -gt $gridW) { Fail $file $lineNo "more than $gridW rooms across" }
        for ($gx = 0; $gx -lt $gridW; $gx++) {
            $text = if ($gx -lt $slots.Count) { $slots[$gx] } else { '' }
            if ($text.Length -gt $roomCols) { Fail $file $lineNo "room $gx,$gy row is $($text.Length) cells wide, max $roomCols" }
            $text = $text.PadRight($roomCols)
            $room = $bySlot["$gx,$gy"]
            if (-not $room) {
                if ($text.Trim(' ~') -ne '') { Warn $file $lineNo "content in empty grid slot $gx,$gy (no 'room $gx $gy' declared)" }
                continue
            }
            $room.Rows[$r] = $lineNo
            for ($c = 0; $c -lt $roomCols; $c++) {
                $ch = $text[$c]
                if ($ch -eq [char]'S') {
                    if ($start) { Fail $file $lineNo 'more than one S' }
                    if ($r -eq 0) { Fail $file $lineNo 'S must be below the top row' }
                    $start = @($room.Index, ($c * 8), ($r * 8 - 8))
                    $ch = [char]' '
                }
                if (-not $tileIds.ContainsKey($ch)) { Fail $file $lineNo "unknown tile '$ch' in room $gx,$gy column $c" }
                $room.Cells[$r * $roomCols + $c] = $tileIds[$ch]
            }
        }
    }
}
if (-not $start) { Fail $file $mapStart "no S (player start) in the map" }

# --- exits and edge checks

function Open($room, $row, $col) { return $solidTiles -notcontains $room.Cells[$row * $roomCols + $col] }

foreach ($room in $rooms) {
    $room.Left = $bySlot["$($room.X - 1),$($room.Y)"]
    $room.Right = $bySlot["$($room.X + 1),$($room.Y)"]
    $room.Up = $bySlot["$($room.X),$($room.Y - 1)"]
    $room.Down = $bySlot["$($room.X),$($room.Y + 1)"]

    # Sideways: doorways must meet doorways
    for ($r = 0; $r -lt $roomRows; $r++) {
        $here = Open $room $r ($roomCols - 1)
        if ($room.Right) {
            if ($here -ne (Open $room.Right $r 0)) {
                Warn $file $room.Rows[$r] "$($room.Name) | $($room.Right.Name): right edge row $r is open on one side only"
            }
        } elseif ($here) {
            Warn $file $room.Rows[$r] "$($room.Name): right edge row $r is open but there is no room beyond"
        }
        if (-not $room.Left -and (Open $room $r 0)) {
            Warn $file $room.Rows[$r] "$($room.Name): left edge row $r is open but there is no room beyond"
        }
    }
    # Vertically: holes in a floor must meet holes in the ceiling below
    if ($room.Down) {
        for ($c = 0; $c -lt $roomCols; $c++) {
            if ((Open $room ($roomRows - 1) $c) -ne (Open $room.Down 0 $c)) {
                Warn $file $room.Rows[$roomRows - 1] "$($room.Name) / $($room.Down.Name): column $c is open on one side only"
            }
        }
    }
}

# --- emit

$c = New-Object Text.StringBuilder
[void]$c.AppendLine('// Generated by tools/build_data.ps1 from data/world.txt - do not edit')
[void]$c.AppendLine('#include "room.h"')
[void]$c.AppendLine()
$table = New-Object Collections.Generic.List[string]
$totalPacked = 0

function Ref($room) { if ($room) { return $room.Index } else { return 'NO_ROOM' } }

foreach ($room in $rooms) {
    # Each byte is (tile << 5) | (run - 1): runs of 1..32 identical cells
    $packed = New-Object Collections.Generic.List[string]
    $cells = $room.Cells
    $i = 0
    while ($i -lt $cells.Count) {
        $t = $cells[$i]; $run = 1
        while ($i + $run -lt $cells.Count -and $run -lt 32 -and $cells[$i + $run] -eq $t) { $run++ }
        $packed.Add('0x{0:X2}' -f (($t -shl 5) -bor ($run - 1)))
        $i += $run
    }
    $totalPacked += $packed.Count

    $id = "room$($room.Index)"
    $escaped = $room.Name.Replace('\', '\\').Replace('"', '\"')
    [void]$c.AppendLine("// $($room.X),$($room.Y) $($room.Name) ($($packed.Count) bytes)")
    [void]$c.AppendLine("static const unsigned char ${id}_map[] = {")
    for ($i = 0; $i -lt $packed.Count; $i += 16) {
        $n = [Math]::Min(16, $packed.Count - $i)
        [void]$c.AppendLine('    ' + (($packed.GetRange($i, $n)) -join ', ') + ',')
    }
    [void]$c.AppendLine('};')
    if ($room.Enemies.Count -gt 0) {
        [void]$c.AppendLine("static const enemy_def ${id}_enemies[] = {")
        foreach ($e in $room.Enemies) { [void]$c.AppendLine($e) }
        [void]$c.AppendLine('};')
        $enemyRef = "${id}_enemies"
    } else {
        $enemyRef = '0'
    }
    [void]$c.AppendLine()
    $table.Add("    { ${id}_map, $(Ref $room.Left), $(Ref $room.Right), $(Ref $room.Up), $(Ref $room.Down), `"$escaped`", $enemyRef, $($room.Enemies.Count) },")
}

[void]$c.AppendLine('const room_t rooms[] = {')
foreach ($t in $table) { [void]$c.AppendLine($t) }
[void]$c.AppendLine('};')
[void]$c.AppendLine()
[void]$c.AppendLine("const unsigned char room_start = $($start[0]);")
[void]$c.AppendLine("const unsigned char room_start_x = $($start[1]);")
[void]$c.AppendLine("const unsigned char room_start_y = $($start[2]);")
Write-Gen 'rooms_data.c' $c.ToString()
$roomSummary = "$($rooms.Count) rooms ($totalPacked bytes packed, $($rooms.Count * $roomCols * $roomRows) raw)"

# -------------------------------------------------------------- sprites

$file = 'data/sprites.txt'
$lines = [IO.File]::ReadAllLines((Join-Path $root $file))
$arts = New-Object Collections.Generic.List[object]
$current = $null
for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i].Trim()
    if ($line -eq '' -or $line.StartsWith(';')) { continue }
    if ($line -match '^\[(\w+)\]$') {
        if ($current -and $current.Rows.Count -ne 16) { Fail $file $current.Line "[$($current.Name)] has $($current.Rows.Count) rows, needs 16" }
        $current = @{ Name = $Matches[1].ToUpper(); Rows = New-Object Collections.Generic.List[int]; Line = $i + 1 }
        $arts.Add($current)
        continue
    }
    if (-not $current) { Fail $file ($i + 1) 'art row before any [name]' }
    if ($line.Length -ne 16) { Fail $file ($i + 1) "row is $($line.Length) pixels wide, needs 16" }
    if ($current.Rows.Count -eq 16) { Fail $file ($i + 1) "[$($current.Name)] has more than 16 rows" }
    $bits = 0
    foreach ($ch in $line.ToCharArray()) { $bits = ($bits -shl 1) -bor [int]($ch -eq [char]'#') }
    $current.Rows.Add($bits)
}
if ($current -and $current.Rows.Count -ne 16) { Fail $file $current.Line "[$($current.Name)] has $($current.Rows.Count) rows, needs 16" }

$h = New-Object Text.StringBuilder
[void]$h.AppendLine('// Generated by tools/build_data.ps1 from data/sprites.txt - do not edit')
[void]$h.AppendLine('#ifndef SPRITE_ART_H')
[void]$h.AppendLine('#define SPRITE_ART_H')
[void]$h.AppendLine()
for ($i = 0; $i -lt $arts.Count; $i++) { [void]$h.AppendLine("#define ART_$($arts[$i].Name) $i") }
[void]$h.AppendLine("#define ART_COUNT $($arts.Count)")
[void]$h.AppendLine()
[void]$h.AppendLine('// One 16-bit word per line, leftmost pixel in bit 15')
[void]$h.AppendLine('extern const unsigned int sprite_art[ART_COUNT][16];')
[void]$h.AppendLine()
[void]$h.AppendLine('#endif')
Write-Gen 'sprite_art.h' $h.ToString()

$s = New-Object Text.StringBuilder
[void]$s.AppendLine('// Generated by tools/build_data.ps1 from data/sprites.txt - do not edit')
[void]$s.AppendLine('#include "sprite_art.h"')
[void]$s.AppendLine()
[void]$s.AppendLine('const unsigned int sprite_art[ART_COUNT][16] = {')
foreach ($a in $arts) {
    [void]$s.AppendLine("    {   // ART_$($a.Name)")
    $words = $a.Rows | ForEach-Object { '0x{0:X4}' -f $_ }
    [void]$s.AppendLine('        ' + (($words[0..7]) -join ', ') + ',')
    [void]$s.AppendLine('        ' + (($words[8..15]) -join ', ') + ',')
    [void]$s.AppendLine('    },')
}
[void]$s.AppendLine('};')
Write-Gen 'sprite_art.c' $s.ToString()

Write-Host "build_data: $roomSummary, $($arts.Count) sprite arts"
