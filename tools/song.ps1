# Compiles the soundtrack, c64/data/music.txt (format in its comments), into
# $gen/song_data.c: each section's voices become streams of (note,
# instrument, frames). Shared by the C64 and Amiga builds, which play it on
# the SID and on Paula. Dot-source it with $root, $gen and Fail defined;
# it leaves a summary in $songSummary and $songBytes.

$file = 'c64/data/music.txt'
$lines = [IO.File]::ReadAllLines((Join-Path $root $file))
$waves = @{ tri = 0x10; saw = 0x20; pulse = 0x40; noise = 0x80 }
$instruments = New-Object Collections.Generic.List[object]
$instIndex = @{}
$drums = @{}
$sections = New-Object Collections.Generic.List[object]
$loop = -1

function Parse-Hex($file, $lineNo, $v, $max) {
    if ($v -notmatch '^[0-9A-Fa-f]+$' -or [Convert]::ToInt32($v, 16) -gt $max) { Fail $file $lineNo "bad hex value '$v'" }
    return [Convert]::ToInt32($v, 16)
}

$semitones = @{ C = 0; D = 2; E = 4; F = 5; G = 7; A = 9; B = 11 }
function Note-Code($file, $lineNo, $name) {
    $n = $semitones[[string]$name[0]]
    if ($name[1] -eq [char]'#') { $n++ } elseif ($name[1] -eq [char]'b') { $n-- }
    $n += ([int][string]$name[$name.Length - 1] - 1) * 12 + 1
    if ($n -lt 1 -or $n -gt 84) { Fail $file $lineNo "note $name is outside C1-B7" }
    return $n
}

# Add a voice line's steps to a voice: @name switches instrument, a drum
# letter plays its instrument, '-' extends the last event
function Add-Steps($file, $lineNo, $voice, $text) {
    foreach ($t in ($text -split '\s+')) {
        if ($t -eq '' -or $t -eq '|') { continue }
        if ($t.StartsWith('@')) {
            $name = $t.Substring(1)
            if (-not $instIndex.ContainsKey($name)) { Fail $file $lineNo "unknown instrument '$name'" }
            $voice.Inst = $instIndex[$name]
            continue
        }
        $ev = $voice.Events
        if ($t -eq '-') {
            if ($ev.Count -eq 0) { Fail $file $lineNo "a voice can't start with '-'" }
            $ev[$ev.Count - 1].Steps++
        } elseif ($t -eq '.') {
            if ($ev.Count -and $ev[$ev.Count - 1].Note -eq 0) { $ev[$ev.Count - 1].Steps++ }
            else { $ev.Add(@{ Note = 0; Inst = 0; Steps = 1 }) }
        } elseif ($drums.ContainsKey($t)) {
            $ev.Add(@{ Note = 1; Inst = $drums[$t]; Steps = 1 })
        } elseif ($t -match '^[A-G][#b]?\d$') {
            if ($voice.Inst -lt 0) { Fail $file $lineNo "a note before any @instrument" }
            $ev.Add(@{ Note = (Note-Code $file $lineNo $t); Inst = $voice.Inst; Steps = 1 })
        } else {
            Fail $file $lineNo "bad step '$t'"
        }
        $voice.Steps++
    }
}

$current = $null
for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = ($lines[$i] -replace ';.*$', '').Trim()
    $lineNo = $i + 1
    if ($line -eq '') { continue }
    $f = $line -split '\s+'
    if ($f[0] -eq 'instrument') {
        if ($f.Count -lt 3 -or -not $waves.ContainsKey($f[2])) { Fail $file $lineNo 'instrument <name> <tri|saw|pulse|noise> [key=value...]' }
        $in = @{ Name = $f[1]; Wave = $waves[$f[2]]; Wave2 = 0; At = 0; AD = 0; SR = 0xF0; PW = 0x800; PWM = 0
                 Arp = @(); Vibrato = 0; Filter = 0; Fixed = 0; Slide = 0; Gate = 0 }
        foreach ($kv in $f[3..($f.Count - 1)]) {
            if ($f.Count -lt 4) { break }
            $k, $v = $kv -split '=', 2
            switch ($k) {
                'ad'      { $in.AD = Parse-Hex $file $lineNo $v 0xFF }
                'sr'      { $in.SR = Parse-Hex $file $lineNo $v 0xFF }
                'pw'      { $in.PW = Parse-Hex $file $lineNo $v 0xFFF }
                'pwm'     { $in.PWM = [int]$v }
                'arp'     { $in.Arp = @($v -split ',' | ForEach-Object { [int]$_ }) }
                'vibrato' { $in.Vibrato = 1 }
                'filter'  { $in.Filter = 1 }
                'fixed'   { $in.Fixed = Parse-Hex $file $lineNo $v 0xFFFF }
                'slide'   { $in.Slide = [int]$v }
                'wave2'   { if (-not $waves.ContainsKey($v)) { Fail $file $lineNo "bad wave '$v'" }; $in.Wave2 = $waves[$v] }
                'at'      { $in.At = [int]$v }
                'gate'    { $in.Gate = [int]$v }
                default   { Fail $file $lineNo "unknown instrument setting '$k'" }
            }
        }
        if ($in.Arp.Count -gt 4) { Fail $file $lineNo 'at most 4 arp notes' }
        if ([Math]::Abs($in.PWM) -gt 127) { Fail $file $lineNo 'pwm must be -127 to 127' }
        $instIndex[$in.Name] = $instruments.Count
        $instruments.Add($in)
    } elseif ($f[0] -eq 'drum') {
        if ($f.Count -ne 3 -or $f[1].Length -ne 1 -or -not $instIndex.ContainsKey($f[2])) { Fail $file $lineNo 'drum <letter> <instrument>' }
        $drums[$f[1]] = $instIndex[$f[2]]
    } elseif ($f[0] -eq 'section') {
        $current = @{ Name = $f[1]; Tick = 0; Line = $lineNo; Voices = @{} }
        foreach ($v in 'A', 'B', 'C') { $current.Voices[$v] = @{ Inst = -1; Events = New-Object Collections.Generic.List[object]; Steps = 0 } }
        $from = $null; $bars = 0
        foreach ($kv in $f[2..($f.Count - 1)]) {
            $k, $v = $kv -split '=', 2
            switch ($k) {
                'tick' { $current.Tick = [int]$v }
                'from' { $from = $v }
                'bars' { $bars = [int]$v }
                'loop' { $loop = $sections.Count }
                default { Fail $file $lineNo "unknown section setting '$k'" }
            }
        }
        if ($current.Tick -lt 1 -or $current.Tick -gt 255) { Fail $file $lineNo 'tick=<1-255> needed' }
        if ($from) { $current.From = $from; $current.Bars = $bars }
        $sections.Add($current)
    } elseif ($line -match '^([ABC]):(.*)$') {
        if (-not $current) { Fail $file $lineNo 'voice line before any section' }
        $letter = $Matches[1]
        $voice = $current.Voices[$letter]
        $text = $Matches[2]
        Add-Steps $file $lineNo $voice $text
        # A 'from' section: the first time a voice is named (with its
        # instrument), bring in that voice's steps from the other song
        if ($current.From -and -not $voice.Imported) {
            $voice.Imported = $true
            $want = $current.Bars * 16
            $got = New-Object Collections.Generic.List[string]
            foreach ($l in [IO.File]::ReadAllLines((Join-Path $root $current.From))) {
                if (($l -replace ';.*$', '').Trim() -match "^${letter}:(.*)$") {
                    foreach ($t in ($Matches[1] -split '\s+')) { if ($t -ne '' -and $t -ne '|' -and $got.Count -lt $want) { $got.Add($t) } }
                }
            }
            if ($got.Count -lt $want) { Fail $file $lineNo "$($current.From) has fewer than $($current.Bars) bars" }
            Add-Steps $current.From 1 $voice ($got -join ' ')
        }
    } else {
        Fail $file $lineNo "expected instrument, drum, section or a voice line"
    }
}
if ($sections.Count -eq 0) { Fail $file 1 'no sections' }
if ($loop -lt 0) { Fail $file 1 'no section marked loop' }

$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine('// Generated by tools/build_c64.ps1 from c64/data/music.txt - do not edit')
[void]$sb.AppendLine('#include "song.h"')
[void]$sb.AppendLine()
[void]$sb.AppendLine('const instrument_t song_instruments[] = {')
foreach ($in in $instruments) {
    $arp = @($in.Arp + @(0, 0, 0, 0))[0..3] -join ', '
    [void]$sb.AppendLine(("    {{ 0x{0:X2}, 0x{1:X2}, {2}, 0x{3:X2}, 0x{4:X2}, 0x{5:X3}, {6}, {7}, {{ {8} }}, {9}, {10}, 0x{11:X4}, {12}, {13} }},   // {14}" -f
        $in.Wave, $in.Wave2, $in.At, $in.AD, $in.SR, $in.PW, $in.PWM, $in.Arp.Count, $arp, $in.Vibrato, $in.Filter, $in.Fixed, $in.Slide, $in.Gate, $in.Name))
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine()
$songBytes = 0
$streamNames = @()
for ($s = 0; $s -lt $sections.Count; $s++) {
    $sec = $sections[$s]
    $names = @()
    foreach ($v in 'A', 'B', 'C') {
        $voice = $sec.Voices[$v]
        if ($voice.Steps -ne $sec.Voices['A'].Steps) {
            Fail $file $sec.Line "section $($sec.Name): voice $v is $($voice.Steps) steps long but voice A is $($sec.Voices['A'].Steps)"
        }
        $bytes = New-Object Collections.Generic.List[string]
        foreach ($e in $voice.Events) {
            $frames = $e.Steps * $sec.Tick
            $note = $e.Note
            while ($frames -gt 255) { $bytes.Add("$note, $($e.Inst), 255"); $frames -= 255; $note = 'SONG_HOLD' }
            $bytes.Add("$note, $($e.Inst), $frames")
        }
        $bytes.Add('SONG_END')
        $songBytes += $bytes.Count * 3 - 2
        $name = "s$($s)_$($v.ToLower())"
        $names += $name
        [void]$sb.AppendLine("static const unsigned char ${name}[] = {   // $($sec.Name)")
        for ($i = 0; $i -lt $bytes.Count; $i += 8) {
            [void]$sb.AppendLine('    ' + ($bytes[$i..([Math]::Min($i + 7, $bytes.Count - 1))] -join ', ') + ',')
        }
        [void]$sb.AppendLine('};')
    }
    $streamNames += '    { ' + ($names -join ', ') + ' },'
}
[void]$sb.AppendLine()
[void]$sb.AppendLine('const unsigned char * const song_streams[][3] = {')
foreach ($n in $streamNames) { [void]$sb.AppendLine($n) }
[void]$sb.AppendLine('};')
[void]$sb.AppendLine("const unsigned char song_sections = $($sections.Count);")
[void]$sb.AppendLine("const unsigned char song_loop = $loop;")
[IO.File]::WriteAllText((Join-Path $gen 'song_data.c'), $sb.ToString())
$songSummary = ($sections | ForEach-Object { '{0} {1:0.0}s' -f $_.Name, ($_.Voices['A'].Steps * $_.Tick / 50) }) -join ', '
