# KaiZXo

![KaiZXo start screen](docs/cover.png)

A tough, precise platformer for the 48K ZX Spectrum, written in C and Z80
assembly with z88dk. Climb, fall and wall-jump through 15 levels of a
haunted castle, from the Garden Gate up to the Crow's Nest, past spikes,
beetles and bats. Each level is one screen: start on the left and reach the
flashing exit in the right-hand wall. Lives are unlimited. When you die you
go back to the start of the screen.

At the start screen, press any jump key to begin.

## Controls

| Action | Keys | Kempston |
| --- | --- | --- |
| Left / right | `O` / `P` | left / right |
| Jump | `Q`, `Space` or `M` | up or fire |
| Drop through a platform | hold `A` | hold down |

The Kempston interface is detected automatically. Keyboard and joystick work
together.

## What's in it

- **Tight movement.** Fixed-point physics at 50Hz, with acceleration, skids
  and air control. Jump height depends on how long you hold the button, and
  you hang for a moment at the top of a full jump.
- **Forgiving timing.** Coyote time lets you jump a few frames after running
  off a ledge. Jump buffering remembers a press made just before landing.
- **Wall slides and wall jumps.** Push into a wall to slow your fall, then
  kick off it.
- **One-way platforms** you can jump up through and drop down through.
- **Spikes and enemies.** Walkers patrol the floors and flyers patrol the
  air. The hurt boxes are smaller than the sprites, so grazes don't kill.
- **A steady climb in difficulty.** The early levels teach jumps, pits and
  spikes. Later ones add one-way platforms, controlled hops under ceiling
  spikes, wall-jump shafts, single-block footholds and faster enemies.
- **A chill soundtrack.** A three-voice lounge loop plays on the AY chip of
  128K machines (or a 48K with an AY interface). A plain 48K plays without
  music.
- **Close to the metal.** The keyboard and joystick ports are read directly.
  A custom IM2 interrupt handler times the frames. Sprites are drawn in
  assembly, and the logic catches up after a slow frame.

## Building

You need:

- [z88dk](https://z88dk.org/) with SDCC, with `zcc` on your `PATH`
- PowerShell, which converts the level and sprite data
- [Fuse](https://fuse-emulator.sourceforge.net/) or any other Spectrum
  emulator, to play it

In VS Code, **Build Spectrum TAP** is the default build task and **Run in
Fuse** is the default test task. From a shell:

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_data.ps1
zcc +zx -vn -compiler=sdcc -SO3 -m -Isrc -Ibuild/gen \
    src/main.c src/player.c src/enemy.c src/room.c src/hud.c src/render.c src/sprites.c src/title.c src/music.c \
    build/gen/rooms_data.c build/gen/sprite_art.c build/gen/title_scr.c build/gen/music_data.c \
    src/render.asm src/input.asm src/interrupts.asm src/sound.asm \
    -o build/game -create-app
fuse --machine 48 --auto-load --tape build/game.tap
```

The build writes `build/game.tap`.

## Making levels

All the levels are in one text file, [data/world.txt](data/world.txt), in
the order they're played. Each one has a name and its enemies, then the
screen drawn as 22 rows of 32 characters:

| Char | Tile |
| --- | --- |
| `#` | brick |
| `X` | block |
| `=` | one-way platform |
| `^` / `v` | floor / ceiling spikes |
| `E` | exit |
| `S` | player start |

Sprites are 16x16 ASCII art in [data/sprites.txt](data/sprites.txt).
`tools/build_data.ps1` packs both files into C data under `build/gen/`, and
reports errors with file and line numbers. The comments at the top of
`world.txt` cover the format and the jump distances to design for.

To check that every level can be finished, run:

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check_levels.ps1
```

It searches for a route frame by frame, using a copy of the player physics
in `tools/level_sim.cs`. It ignores enemies. For each level it reports the
fastest time. It also reports how many frames each input can be held
before it changes and the level can still be finished, which is a rough
guide to how forgiving the level is. Use `-Level <n>` to check one level.
If you change the physics in `src/player.c`, change `level_sim.cs` to match.

## Music

The soundtrack is [data/music.txt](data/music.txt), written as a grid of
steps like a tracker: a note, `-` to hold it, or `.` for silence, for each
of the three voices. Each voice has a simple instrument: a starting volume
that fades to a floor. `build_data.ps1` turns it into note data and pitch
tables for the AY.
## Project layout

```
src/         game code: C, plus Z80 assembly for input, interrupts and drawing
data/        levels, sprite art, music and start screen
tools/       data converter and start-screen generator
docs/        README cover image
```

To show CPU use as coloured bars in the border, set `DEBUG_TIMING` to 1 in
`src/main.c`.

## The start screen

The start screen, [data/title.scr](data/title.scr), is a real Spectrum screen
with one ink and one paper colour per 8x8 cell. `tools/make_cover.ps1` draws
it from the game's own tiles and sprites and the ROM font. The script also
saves the same screen as [docs/cover.png](docs/cover.png) for this README.
The build compresses the screen with zx0, to under 1KB, and the game unpacks
it straight into screen memory. The game prints the blinking "PRESS SPACE TO
START" prompt itself.

Run the script again after changing it, or the tiles or sprites:

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/make_cover.ps1
```

The script reads the font from a 48K ROM image. By default it looks in
Fuse's install folder. Use `-Rom` to point it somewhere else.
