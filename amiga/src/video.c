#include "gcc8_c_support.h"
#include <proto/exec.h>
#include <exec/memory.h>
#include <hardware/dmabits.h>
#include <hardware/blit.h>
#include "video.h"
#include "hud.h"
#include "hw.h"
#include "roomgfx.h"
#include "gfx_data.h"

#define REG(r)      ((UWORD)offsetof(struct Custom, r))

// Front playfield: the world with 16 pixels of margin either side for the
// scroll, 3 planes interleaved (each line is plane 0, 1, 2)
#define PF1_ROWBYTES ((16 + WORLD_W + 16) / 8)
#define PF1_STRIDE   (PF1_ROWBYTES * 3)
#define PF1_SIZE     (PF1_STRIDE * WORLD_H)
// Back playfield: the generated wall or towers (gfx_data.c)
#define PF2_STRIDE   (BG_ROWBYTES * 3)

// Each line fetches 21 words: one more than the screen, for the scroll
#define FETCH_BYTES  42

#define DISPLAY_TOP  0x2c       // first line of the screen
#define HUD_LINE     (DISPLAY_TOP + VIEW_H)
#define COPPER_WORDS 2000

static UBYTE *worlds[2];        // double buffered
static UBYTE front, back;       // which is shown, which is drawn into
static UWORD *copper;
static UWORD *cop_sprites;      // the 8 sprite pointer moves
static UWORD *cop_planes;       // the 6 bitplane pointer moves
static UWORD *cop_bplcon1;
static UWORD *cop_sky[VIEW_H];  // colour 0's value, for each line
static UWORD *cop_sky_top;      // ...and above the screen
static UWORD *cop_line5[VIEW_H]; // colour 5 for each line (the titles logo shines)
static UWORD *cop_colours;      // colours 1-15 (the HUD changes some)

static UWORD null_sprite[2] __attribute__((section(".MEMF_CHIP")));

#define MAX_BOBS 4

// Where enemies were drawn in each buffer, in cells, to put back
typedef struct { UBYTE c0, r0, c1, r1; } cells_t;
static cells_t drawn[2][MAX_BOBS];
static UBYTE drawn_count[2];

// ------------------------------------------------------------- themes

typedef struct {
    UWORD tiles[8];             // front playfield 1-7: 6 is the danger colour
    UWORD wall[8];              // back playfield 1-7
    UWORD sky[4];               // gradient, top to bottom
    UBYTE outdoor;              // towers rather than the castle wall
    UBYTE storm;                // lightning
} theme_t;

// One per level, in world.txt order: down in the castle, then out on the
// towers, then up into the storm
static const theme_t themes[] = {
    // The Kitchen: warm brick, dusk through the windows
    { { 0, 0x210, 0x421, 0x732, 0xa53, 0xd85, 0xc3b, 0xfe9 }, { 0, 0x100, 0x211, 0x322, 0x433, 0x544, 0, 0 },
      { 0x102, 0x214, 0x625, 0xb63 }, 0, 0 },
    // The Cellar Passage: damp grey-brown
    { { 0, 0x111, 0x322, 0x543, 0x765, 0x987, 0xd43, 0xfdb }, { 0, 0x000, 0x111, 0x211, 0x222, 0x333, 0, 0 },
      { 0x001, 0x012, 0x123, 0x234 }, 0, 0 },
    // The Crypt: cold stone, a sickly glow
    { { 0, 0x111, 0x333, 0x555, 0x777, 0x9a9, 0x6c4, 0xdfd }, { 0, 0x000, 0x111, 0x121, 0x232, 0x343, 0, 0 },
      { 0x000, 0x011, 0x022, 0x143 }, 0, 0 },
    // The Wine Cellar
    { { 0, 0x200, 0x401, 0x613, 0x834, 0xa56, 0xd6e, 0xfcd }, { 0, 0x100, 0x201, 0x302, 0x413, 0x524, 0, 0 },
      { 0x101, 0x202, 0x413, 0x724 }, 0, 0 },
    // The Chimney Stack: soot, and fire below
    { { 0, 0x100, 0x221, 0x332, 0x443, 0x664, 0xf60, 0xfd4 }, { 0, 0x000, 0x110, 0x210, 0x321, 0x431, 0, 0 },
      { 0x200, 0x410, 0x830, 0xf80 }, 0, 0 },
    // The Belfry: old wood and bronze
    { { 0, 0x210, 0x431, 0x642, 0x863, 0xb95, 0xc4c, 0xfeb }, { 0, 0x100, 0x210, 0x320, 0x431, 0x542, 0, 0 },
      { 0x013, 0x135, 0x358, 0x69b }, 0, 0 },
    // The Crow's Nest: out under the night sky
    { { 0, 0x112, 0x334, 0x556, 0x778, 0x99a, 0xd4d, 0xffe }, { 0, 0x001, 0x112, 0x223, 0x334, 0xdb6, 0, 0 },
      { 0x001, 0x013, 0x136, 0x358 }, 1, 0 },
    // Rooftops: red tiles at sunset
    { { 0, 0x211, 0x522, 0x833, 0xb54, 0xd86, 0x4ce, 0xffe }, { 0, 0x101, 0x212, 0x323, 0x434, 0xfc6, 0, 0 },
      { 0x113, 0x336, 0x957, 0xf96 }, 1, 0 },
    // The Clock Tower
    { { 0, 0x112, 0x445, 0x667, 0x99a, 0xccd, 0xe52, 0xfff }, { 0, 0x001, 0x112, 0x223, 0x334, 0xdb6, 0, 0 },
      { 0x003, 0x115, 0x337, 0x559 }, 1, 0 },
    // The Battlements: moonlight
    { { 0, 0x111, 0x334, 0x556, 0x889, 0xaab, 0xe4e, 0xfff }, { 0, 0x012, 0x123, 0x234, 0x345, 0xec7, 0, 0 },
      { 0x024, 0x147, 0x38a, 0x7bd }, 1, 0 },
    // The Gargoyle Ledge: clouds close in
    { { 0, 0x111, 0x333, 0x455, 0x677, 0x899, 0x8e4, 0xeff }, { 0, 0x111, 0x222, 0x333, 0x444, 0x998, 0, 0 },
      { 0x111, 0x222, 0x334, 0x556 }, 1, 0 },
    // The Spire: the storm begins
    { { 0, 0x012, 0x124, 0x236, 0x458, 0x68b, 0xee4, 0xfff }, { 0, 0x001, 0x012, 0x123, 0x234, 0x885, 0, 0 },
      { 0x000, 0x012, 0x124, 0x236 }, 1, 1 },
    // The Weathervane
    { { 0, 0x111, 0x333, 0x556, 0x778, 0xaab, 0xfc3, 0xfff }, { 0, 0x001, 0x111, 0x212, 0x323, 0x885, 0, 0 },
      { 0x000, 0x101, 0x213, 0x325 }, 1, 1 },
    // The Lightning Rod
    { { 0, 0x012, 0x124, 0x247, 0x479, 0x7ac, 0xff6, 0xfff }, { 0, 0x000, 0x011, 0x122, 0x233, 0x885, 0, 0 },
      { 0x000, 0x002, 0x014, 0x126 }, 1, 1 },
    // Eye of the Storm
    { { 0, 0x102, 0x214, 0x426, 0x648, 0x96b, 0xfe4, 0xfff }, { 0, 0x101, 0x202, 0x313, 0x424, 0x885, 0, 0 },
      { 0x000, 0x102, 0x305, 0x518 }, 1, 1 },
};
#define THEME_COUNT (sizeof(themes) / sizeof(themes[0]))

// The hero (colours 17-23): outline, hood dark/mid/light, skin dark/mid/light
static const UWORD hero_colours[3][7] = {
    { 0x112, 0x146, 0x28a, 0x6cd, 0xa64, 0xdb8, 0xfed },
    { 0x445, 0xbbb, 0xddd, 0xfff, 0xbbb, 0xddd, 0xfff },    // white flash
    { 0x200, 0x600, 0xa00, 0xf44, 0x822, 0xb44, 0xf88 },    // red
};

// Lightning: the sky flashes through these
static const UWORD flash_sky[4] = { 0x8af, 0xbdf, 0xdef, 0xfff };

static const theme_t *theme = themes;
static UBYTE brightness = 16;
static UBYTE hero_tint;
static UBYTE flash;             // frames of lightning left
static UWORD next_strike;
static UBYTE *background;

static UWORD rnd_state = 0x5eed;
static UWORD rnd(void) {
    rnd_state ^= rnd_state << 7;
    rnd_state ^= rnd_state >> 9;
    rnd_state ^= rnd_state << 8;
    return rnd_state;
}

// Scale a colour by the brightness (0-16)
// The skys gradient, worked out once a level (normal and lit by lightning),
// and a table for dimming: scaled[b][v] is v (0-15) at brightness b (0-16).
// Fades and lightning then cost a few lookups a line, not divisions.
static UWORD sky_lines[2][VIEW_H];
static UWORD line5[VIEW_H];
// ...and both at every brightness, so a fade step is only a copy
static UWORD sky_faded[17][VIEW_H], line5_faded[17][VIEW_H];
static UBYTE scaled[17][16];

static UWORD dim_to(UWORD c, UBYTE b) {
    const UBYTE *t = scaled[b];

    return t[(c >> 8) & 15] << 8 | t[(c >> 4) & 15] << 4 | t[c & 15];
}

static UWORD dim(UWORD c) {
    return dim_to(c, brightness);
}

static void prepare_fades(void) {
    for (UBYTE b = 0; b <= 16; b++) {
        for (UWORD y = 0; y < VIEW_H; y++) {
            sky_faded[b][y] = dim_to(sky_lines[0][y], b);
            line5_faded[b][y] = dim_to(line5[y], b);
        }
    }
}

static UWORD blend(UWORD a, UWORD b, UWORD t, UWORD n) {
    UWORD out = 0;

    for (UWORD s = 0; s < 12; s += 4) {
        int ca = (a >> s) & 15, cb = (b >> s) & 15;
        out |= (UWORD)(ca + (cb - ca) * (int)t / (int)n) << s;
    }
    return out;
}

static void apply_colours(void) {
    // A lightning strike flickers: lit, dark, lit, dark (only ever in play,
    // at full brightness)
    const UWORD *sky = (flash & 2) ? sky_lines[1] : sky_faded[brightness];
    const UWORD *l5 = line5_faded[brightness];

    for (int i = 1; i < 8; i++) {
        cop_colours[(i - 1) * 2 + 1] = dim(theme->tiles[i]);
        cop_colours[(i + 7) * 2 + 1] = dim(theme->wall[i]);
    }
    for (int i = 0; i < 7; i++) custom->color[17 + i] = dim(hero_colours[hero_tint][i]);

    *cop_sky_top = sky[0];
    for (UWORD y = 0; y < VIEW_H; y++) {
        *cop_sky[y] = sky[y];
        *cop_line5[y] = l5[y];
    }
}

static void set_theme(const theme_t *t) {
    theme = t;
    background = theme->outdoor ? bg_outdoor : bg_indoor;
    flash = 0;
    next_strike = 120;

    // Three bands of gradient down the screen
    for (UWORD y = 0; y < VIEW_H; y++) {
        UWORD band = y * 3 / VIEW_H, at = y - band * VIEW_H / 3;
        sky_lines[0][y] = blend(theme->sky[band], theme->sky[band + 1], at, VIEW_H / 3 + 1);
        sky_lines[1][y] = blend(flash_sky[band], flash_sky[band + 1], at, VIEW_H / 3 + 1);
        line5[y] = theme->tiles[5];
    }
}

void video_theme(unsigned char level) {
    set_theme(&themes[level % THEME_COUNT]);
    prepare_fades();
    apply_colours();
}

// The title: night over the towers, and a logo in gold shading to purple
static const theme_t title_theme = {
    { 0, 0x102, 0x325, 0x649, 0x96c, 0xfd6, 0xf8c, 0xfff }, { 0, 0x001, 0x112, 0x223, 0x334, 0xdb6, 0, 0 },
    { 0x001, 0x013, 0x136, 0x48a }, 1, 0
};
static const UWORD logo_shine[4] = { 0xffe, 0xfd6, 0xe84, 0xa4a };

void video_theme_title(int logo_top, int logo_height) {
    set_theme(&title_theme);
    for (int y = 0; y < logo_height && logo_top + y < VIEW_H; y++) {
        UWORD band = y * 3 / logo_height, at = y - band * logo_height / 3;
        line5[logo_top + y] = blend(logo_shine[band], logo_shine[band + 1], at, logo_height / 3 + 1);
    }
    prepare_fades();
    apply_colours();
}

void video_brightness(unsigned char level) {
    brightness = level;
    apply_colours();
}

void video_hero_tint(unsigned char tint) {
    hero_tint = tint;
    for (int i = 0; i < 7; i++) custom->color[17 + i] = dim(hero_colours[tint][i]);
}

int video_animate(void) {
    if (!theme->storm) return 0;
    if (flash) {
        // Only redo the colours when the flicker changes
        UBYTE was = flash & 2;
        flash--;
        if ((flash & 2) != was) apply_colours();
    } else if (!--next_strike) {
        flash = 8;              // lights up next frame (flash & 2)
        next_strike = 150 + (rnd() & 255);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- set-up

static UWORD *move(UWORD *c, UWORD reg, UWORD value) {
    *c++ = reg;
    *c++ = value;
    return c;
}

static UWORD *wait_line(UWORD *c, UWORD line) {
    *c++ = ((line & 0xff) << 8) | 0x07;
    *c++ = 0xfffe;
    return c;
}

void video_init(void) {
    UWORD *c;

    worlds[0] = (UBYTE *)AllocMem(PF1_SIZE, MEMF_CHIP | MEMF_CLEAR);
    worlds[1] = (UBYTE *)AllocMem(PF1_SIZE, MEMF_CHIP | MEMF_CLEAR);
    copper = (UWORD *)AllocMem(COPPER_WORDS * 2, MEMF_CHIP | MEMF_CLEAR);
    front = 0;
    back = 1;
    for (int b = 0; b <= 16; b++) {
        for (int v = 0; v < 16; v++) scaled[b][v] = v * b >> 4;
    }
    background = bg_indoor;

    c = copper;
    // Wait a little into the vertical blank, so the game has updated the
    // pointers below after the frame interrupt before they're used, but
    // not so long that the sprite DMA has already fetched its control
    // words for the frame (at line $19), or the sprites miss a frame
    *c++ = 0x1001;
    *c++ = 0xfffe;
    cop_sky_top = c + 1;
    c = move(c, REG(color[0]), 0);
    cop_colours = c;
    for (int i = 1; i < 16; i++) c = move(c, REG(color[i]), 0);
    c = move(c, REG(diwstrt), (DISPLAY_TOP << 8) | 0x81);
    c = move(c, REG(diwstop), ((DISPLAY_TOP + VIEW_H + HUD_HEIGHT - 256) << 8) | 0xc1);
    c = move(c, REG(ddfstrt), 0x0030);
    c = move(c, REG(ddfstop), 0x00d0);
    c = move(c, REG(bplcon0), 0x6600);      // 6 planes, dual playfield, colour
    c = move(c, REG(bplcon2), 0x0024);      // sprites in front of both playfields
    c = move(c, REG(bpl1mod), PF1_STRIDE - FETCH_BYTES);
    c = move(c, REG(bpl2mod), PF2_STRIDE - FETCH_BYTES);
    cop_bplcon1 = c + 1;
    c = move(c, REG(bplcon1), 0);
    cop_sprites = c;
    for (int i = 0; i < 8; i++) {
        c = move(c, REG(sprpt[i]), (UWORD)((ULONG)null_sprite >> 16));
        c = move(c, REG(sprpt[i]) + 2, (UWORD)(ULONG)null_sprite);
    }
    cop_planes = c;
    for (int i = 0; i < 6; i++) {
        c = move(c, REG(bplpt[i]), 0);
        c = move(c, REG(bplpt[i]) + 2, 0);
    }
    // The sky: colour 0, changed every line
    for (UWORD y = 0; y < VIEW_H; y++) {
        UWORD line = DISPLAY_TOP + y;
        if (line == 256) {
            *c++ = 0xffdf;          // past line 255: the wait wraps
            *c++ = 0xfffe;
        }
        c = wait_line(c, line);
        cop_sky[y] = c + 1;
        c = move(c, REG(color[0]), 0);
        cop_line5[y] = c + 1;
        c = move(c, REG(color[5]), 0);
    }
    // The HUD: its own little screen below. Switch in the right border of
    // the line before, so its all set before the HUDs first line.
    *c++ = (((HUD_LINE - 1) & 0xff) << 8) | 0xdf;
    *c++ = 0xfffe;
    c = hud_copper(c);
    *c++ = 0xffff;
    *c++ = 0xfffe;
}

void video_start(void) {
    video_scroll(0, 0);
    custom->cop1lc = (ULONG)copper;
    custom->copjmp1 = 0x7fff;
    custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_SPRITE | DMAF_BLITTER;
}

// ----------------------------------------------------------------- world

void video_clear_world(void) {
    wait_blit();
    memclr(worlds[0], PF1_SIZE);
    memclr(worlds[1], PF1_SIZE);
    drawn_count[0] = drawn_count[1] = 0;
}

static void draw_cell_in(UBYTE *world, unsigned char col, unsigned char row, unsigned char tile) {
    const UWORD *src = tile_gfx[tile];
    UBYTE *dst = world + (UWORD)row * 16 * PF1_STRIDE + 2 + col * 2;

    for (int y = 0; y < 16; y++, dst += PF1_STRIDE) {
        *(UWORD *)dst = *src++;
        *(UWORD *)(dst + PF1_ROWBYTES) = *src++;
        *(UWORD *)(dst + PF1_ROWBYTES * 2) = *src++;
    }
}

void video_draw_cell(unsigned char col, unsigned char row, unsigned char tile) {
    wait_blit();
    draw_cell_in(worlds[0], col, row, tile);
    draw_cell_in(worlds[1], col, row, tile);
}

void video_plot(int x, int y, unsigned char colour) {
    ULONG at;
    UBYTE bit;

    if (x < 0 || y < 0 || x >= WORLD_W || y >= WORLD_H) return;
    at = (ULONG)y * PF1_STRIDE + ((x + 16) >> 3);
    bit = 0x80 >> ((x + 16) & 7);
    for (int w = 0; w < 2; w++) {
        for (int p = 0; p < 3; p++) {
            UBYTE *b = worlds[w] + at + p * PF1_ROWBYTES;
            *b = (colour & (1 << p)) ? (*b | bit) : (*b & ~bit);
        }
    }
}

// --------------------------------------------------------------- scroll

// The fetch starts a word early, so with no delay the screen's left edge
// shows bitmap pixel (pointer * 8 + 16). Each bitmap has 16 pixels of
// margin on the left, so pixel x of the picture is at bitmap x + 16.
static ULONG scroll_pointer(const UBYTE *base, UWORD stride, int x, int y, UWORD *delay) {
    *delay = (16 - (x & 15)) & 15;
    return (ULONG)(base + (ULONG)y * stride + ((x + 15) >> 4) * 2);
}

static void set_plane(int i, ULONG addr) {
    cop_planes[i * 4 + 1] = (UWORD)(addr >> 16);
    cop_planes[i * 4 + 3] = (UWORD)addr;
}

void video_scroll(int cam_x, int cam_y) {
    video_scroll_both(cam_x, cam_y, cam_x >> 1, cam_y >> 1);
}

void video_scroll_both(int cam_x, int cam_y, int bg_x, int bg_y) {
    UWORD d1, d2;
    ULONG p1 = scroll_pointer(worlds[front], PF1_STRIDE, cam_x, cam_y, &d1);
    ULONG p2 = scroll_pointer(background, PF2_STRIDE, bg_x, bg_y, &d2);

    // Odd planes (1, 3, 5) are the front playfield, even the back
    for (int p = 0; p < 3; p++) {
        set_plane(p * 2, p1 + p * PF1_ROWBYTES);
        set_plane(p * 2 + 1, p2 + p * BG_ROWBYTES);
    }
    *cop_bplcon1 = d1 | (d2 << 4);
}

// ----------------------------------------------------------------- hero

static void set_sprite(int i, const void *data) {
    cop_sprites[i * 4 + 1] = (UWORD)((ULONG)data >> 16);
    cop_sprites[i * 4 + 3] = (UWORD)(ULONG)data;
}

void video_hero(int x, int y, int frame) {
    if (frame < 0 || y <= -HERO_HEIGHT || y >= VIEW_H || x <= -32 || x >= VIEW_W) {
        for (int i = 0; i < 4; i++) set_sprite(i, null_sprite);
        return;
    }
    // Stop at the HUD
    UWORD vstart = DISPLAY_TOP + y, vstop = vstart + HERO_HEIGHT;
    if (vstop > HUD_LINE) vstop = HUD_LINE;
    for (int i = 0; i < 4; i++) {
        UWORD *s = hero_sprites[frame][i];
        UWORD hstart = 0x80 + x + (i >> 1) * 16;
        s[0] = ((vstart & 0xff) << 8) | ((hstart >> 1) & 0xff);
        s[1] = ((vstop & 0xff) << 8) | ((i & 1) ? 0x80 : 0) |
               ((vstart >> 8) & 1) << 2 | ((vstop >> 8) & 1) << 1 | (hstart & 1);
        set_sprite(i, s);
    }
}

// --------------------------------------------------------------- enemies

void video_bobs_begin(void) {
    UBYTE *world = worlds[back];

    wait_blit();
    for (int i = 0; i < drawn_count[back]; i++) {
        cells_t *d = &drawn[back][i];
        for (UBYTE r = d->r0; r <= d->r1; r++) {
            for (UBYTE c = d->c0; c <= d->c1; c++) {
                draw_cell_in(world, c, r, room_cell_tile((UWORD)r * 32 + c));
            }
        }
    }
    drawn_count[back] = 0;
}

// Blitter: D = A ? B : C, where A is the mask, B the enemy and C the level
// underneath, all shifted right by x & 15. 32 lines of 3 planes, 3 words.
void video_bob(int x, int y, unsigned char frame) {
    UBYTE *dst;
    UWORD shift = x & 15;
    cells_t *d;
    int r1 = (y + 31) >> 4;

    if (drawn_count[back] == MAX_BOBS || x < 0 || y < 0 || x > WORLD_W - 32 || y > WORLD_H - 32) return;
    d = &drawn[back][drawn_count[back]++];
    d->c0 = x >> 4;
    d->c1 = (x + 31) >> 4;
    d->r0 = y >> 4;
    d->r1 = r1 > 21 ? 21 : r1;

    dst = worlds[back] + (ULONG)y * PF1_STRIDE + 2 + (x >> 4) * 2;
    wait_blit();
    custom->bltcon0 = SRCA | SRCB | SRCC | DEST | 0xca | (shift << ASHIFTSHIFT);
    custom->bltcon1 = shift << BSHIFTSHIFT;
    custom->bltafwm = 0xffff;
    custom->bltalwm = 0xffff;
    custom->bltapt = (APTR)bob_gfx[frame][1];
    custom->bltbpt = (APTR)bob_gfx[frame][0];
    custom->bltcpt = dst;
    custom->bltdpt = dst;
    custom->bltamod = 0;
    custom->bltbmod = 0;
    custom->bltcmod = PF1_ROWBYTES - 6;
    custom->bltdmod = PF1_ROWBYTES - 6;
    custom->bltsize = ((32 * 3) << HSIZEBITS) | 3;
}

void video_bobs_end(void) {
    UBYTE t = front;

    front = back;
    back = t;
}
