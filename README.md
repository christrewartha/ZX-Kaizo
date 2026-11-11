# KaiZXo

![KaiZXo start screen](docs/cover.png)

A tough, precise platformer for the 48K ZX Spectrum, written in C and Z80
assembly with z88dk. Climb, fall and wall-jump through 15 levels of a
haunted castle, from the Kitchen up into the Eye of the Storm, past spikes,
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
- **Hard from the start, then harder.** The first eight levels mix pits,
  spikes, wall-jump shafts and enemies. The last seven need near-perfect
  play: hops over walkers under ceiling spikes, long jumps between single
  blocks, wall jumps past spiked walls, and landings on one-way platforms
  with spikes just above them.
- **A chill soundtrack.** A three-voice lounge loop plays on the AY chip of
  128K machines (or a 48K with an AY interface). A plain 48K plays without
  music.
- **Close to the metal.** The keyboard and joystick ports are read directly.
  A custom IM2 interrupt handler times the frames. Sprites are drawn in
  assembly, top to bottom and a line at a time, so none vanish ahead of the
  beam. The logic catches up after a slow frame.

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
    -o build/game -create-app -Cz--audio -Cz--screen=data/loading.scr
fuse --machine 48 --auto-load --tape build/game.tap
```

The build writes `build/game.tap` for emulators and `build/game.wav` for a
real Spectrum.

### Loading on a real Spectrum

Connect your PC or phone's headphone socket to the Spectrum's **EAR** socket.
Type `LOAD ""` and press Enter. On a 128K machine, pick **Tape Loader**
instead. Then play `build/game.wav`. Loading takes about 3 minutes.
If it fails, try a different volume and turn off any audio effects or EQ.

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
in `tools/level_sim.cs`, with the enemies on their patrols. For each level
it reports how long the route takes (for a level with no enemies, the
fastest possible). It also reports how many frames each input can be held
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
data/        levels, sprite art, music, start screen and loading screen
tools/       data converter, screen generators and web page build
docs/        README images
web/         web page template and the JSSpeccy emulator
deploy/      service and one-time setup for the web server
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

## The loading screen

On tape, the game loads a picture first, [data/loading.scr](data/loading.scr),
which fills in line by line while the rest of the game loads underneath.
`tools/make_loading.ps1` paints it in the 16 Spectrum colours, then cuts each
8x8 cell down to the two colours the hardware allows. It warns about any cell
that held more than two colours, so you can move things until nothing clashes.
It also saves [docs/loading.png](docs/loading.png).

![KaiZXo loading screen](docs/loading.png)

Run the script again after changing it:

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/make_loading.ps1
```

## Playing in a browser

The game can run in a web page, on the
[JSSpeccy 3](https://github.com/gasman/jsspeccy3) emulator. The page template
is [web/index.html](web/index.html). A copy of JSSpeccy 3.2 is in
`web/jsspeccy/`, under its GPL v3 licence.

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_web.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/serve_web.ps1
```

`build_web.ps1` puts the page, the emulator and the latest `build/game.tap`
into `build/web/`, which any static web server can serve. The tape's file
name includes a hash of it, so browsers never load an old build.
`serve_web.ps1` serves that folder on http://localhost:8080/, so you can try
it before deploying. Browsers won't run the emulator from a `file://` URL.
In VS Code, the **Build Web Page** and **Serve Web Page** tasks do the same.

The page runs the game on a 128K Spectrum, so the AY music plays. It starts
when you click the loading screen, because browsers only allow sound after a
click.

On phones and tablets, the page shows touch controls: a d-pad, plus JUMP,
DROP and START buttons. Each one sends the emulator the same key events as
the keyboard. Turned sideways, the page shows just the game, with the
controls on either side. `build_web.ps1` also draws the site's icons from
the hero's jump sprite.

### Deploying

The page is live at https://kaizxo.christrewartha.com/. It's served from the
home server `scaramanga`, through the same Cloudflare tunnel as NFFL.

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/deploy_web.ps1
```

The **Deploy Web Page** task builds everything first, then runs this. The
script uploads `build/web/` over SSH into a new folder under
`~/kaizxo/releases/`, then switches the `~/kaizxo/site` link to it. It keeps
the last 5 releases. To roll back, point the link at an older one:
`ln -sfn releases/<time> ~/kaizxo/site`. It needs SSH key login to the
server.

On the server, `kaizxo.service` runs Python's built-in web server on
`127.0.0.1:8790`, and the tunnel sends the hostname there. To set that up the
first time, deploy once and then run this on the server:

```sh
sudo bash ~/kaizxo/setup_server.sh
```

It installs and starts the service, then adds the hostname to
`/etc/cloudflared/config.yml` and restarts the tunnel. It keeps a backup of the
old config. The DNS record came from
`cloudflared tunnel route dns <tunnel> kaizxo.christrewartha.com`.
