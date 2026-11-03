# KaiZXo

![KaiZXo start screen](docs/cover.png)

A tough, precise platformer for the 48K ZX Spectrum, written in C and Z80
assembly with z88dk. Climb, fall and wall-jump through 15 rooms of a
haunted castle, from the Belfry down to the Crypt, past spikes, beetles and
bats. Lives are unlimited. When you die you go straight back to the last
place you stood safely.

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
- **A connected world.** Rooms sit on a 4x4 grid. Walk off any edge that has
  a neighbour to move to the next room.
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
    src/main.c src/player.c src/enemy.c src/room.c src/hud.c src/render.c src/sprites.c src/title.c \
    build/gen/rooms_data.c build/gen/sprite_art.c build/gen/title_scr.c \
    src/render.asm src/input.asm src/interrupts.asm \
    -o build/game -create-app
fuse --machine 48 --auto-load --tape build/game.tap
```

The build writes `build/game.tap`.

## Making levels

The whole world is one text file, [data/world.txt](data/world.txt). It
declares each room's name and enemies, then draws all the rooms on a grid
exactly as they connect:

| Char | Tile |
| --- | --- |
| `#` | brick |
| `X` | block |
| `=` | one-way platform |
| `^` / `v` | floor / ceiling spikes |
| `S` | player start |

Sprites are 16x16 ASCII art in [data/sprites.txt](data/sprites.txt).
`tools/build_data.ps1` packs both files into C data under `build/gen/`. It
reports errors and warnings with file and line numbers, including doorways
that don't line up with the room next door. The comments at the top of
`world.txt` cover the format and the jump distances to design for.

## Project layout

```
src/         game code: C, plus Z80 assembly for input, interrupts and drawing
data/        world map, sprite art and start screen
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
