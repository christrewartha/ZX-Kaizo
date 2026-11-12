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

// The world, with 16 pixels of margin either side for the scroll, its 5
// planes interleaved (each line is plane 0, 1, 2, 3, 4)
#define ROWBYTES    ((16 + WORLD_W + 16) / 8)
#define STRIDE      (ROWBYTES * PLANES)
#define WORLD_SIZE  ((ULONG)STRIDE * WORLD_H)

// Each line fetches 21 words: one more than the screen, for the scroll
#define FETCH_BYTES 42

#define DISPLAY_TOP 0x2c        // first line of the screen
#define HUD_LINE    (DISPLAY_TOP + VIEW_H)
#define COPPER_WORDS 2000

static UBYTE *worlds[2];        // double buffered
static UBYTE front, back;       // which is shown, which is drawn into
static UWORD *copper;
static UWORD *cop_sprites;      // the 8 sprite pointer moves
static UWORD *cop_planes;       // the 5 bitplane pointer moves
static UWORD *cop_bplcon1;
static UWORD *cop_colours;      // colours 1-31 (the HUD changes some)
static UWORD *cop_sky_top;      // colour 0 above the screen...
static UWORD *cop_sky[VIEW_H];  // ...and on each line
static UWORD *cop_shine[VIEW_H]; // colour 31 on each line (the title's logo)

static UWORD null_sprite[2] __attribute__((section(".MEMF_CHIP")));

#define MAX_BOBS 4

// Where enemies were drawn in each buffer, and what was there before (the
// blitter saves it, and puts it back next time round)
typedef struct { UBYTE *dst; UWORD *save; UWORD words, height; } drawn_t;
static drawn_t drawn[2][MAX_BOBS];
static UBYTE drawn_count[2];

// -------------------------------------------------------------- themes

typedef struct {
    UWORD sky[4];               // gradient, top to bottom
    UBYTE tint;                 // the art's brightness, 0-16 (dusk, night...)
    UBYTE outdoor;              // earth and grass, or the ruins
    UBYTE storm;                // lightning
} theme_t;

// One per level, in world.txt order: down in the ruins, then out in the
// sun, through dusk and night, and up into the storm
static const theme_t themes[] = {
    { { 0x101, 0x212, 0x323, 0x434 }, 16, 0, 0 },     // The Kitchen
    { { 0x001, 0x112, 0x223, 0x334 }, 15, 0, 0 },     // The Cellar Passage
    { { 0x000, 0x011, 0x122, 0x233 }, 14, 0, 0 },     // The Crypt
    { { 0x101, 0x202, 0x313, 0x424 }, 15, 0, 0 },     // The Wine Cellar
    { { 0x200, 0x410, 0x720, 0xa40 }, 15, 0, 0 },     // The Chimney Stack: fire below
    { { 0x112, 0x224, 0x346, 0x568 }, 15, 0, 0 },     // The Belfry
    { { 0x29f, 0x4cf, 0x8ef, 0xcff }, 16, 1, 0 },     // The Crow's Nest: out in the sun
    { { 0x34a, 0x96b, 0xe96, 0xfd8 }, 15, 1, 0 },     // Rooftops: sunset
    { { 0x2af, 0x4df, 0x9ef, 0xdff }, 16, 1, 0 },     // The Clock Tower
    { { 0x125, 0x348, 0x76a, 0xb8a }, 13, 1, 0 },     // The Battlements: dusk
    { { 0x678, 0x899, 0xaab, 0xccd }, 14, 1, 0 },     // The Gargoyle Ledge: mist
    { { 0x000, 0x112, 0x224, 0x336 }, 11, 1, 1 },     // The Spire: the storm
    { { 0x000, 0x101, 0x213, 0x325 }, 11, 1, 1 },     // The Weathervane
    { { 0x000, 0x002, 0x014, 0x126 }, 11, 1, 1 },     // The Lightning Rod
    { { 0x000, 0x102, 0x305, 0x518 }, 11, 1, 1 },     // Eye of the Storm
};
#define THEME_COUNT (sizeof(themes) / sizeof(themes[0]))

// The title: a clear morning
static const theme_t title_theme = { { 0x29f, 0x4cf, 0x8ef, 0xcff }, 16, 1, 0 };
static const UWORD logo_shine[4] = { 0xffe, 0xfd6, 0xe84, 0xa4a };

// Lightning: the sky flashes through these
static const UWORD flash_sky[4] = { 0x8af, 0xbdf, 0xdef, 0xfff };

static const theme_t *theme = themes;
static UBYTE brightness = 16;
static UBYTE flash;             // frames of lightning left
static UWORD next_strike;

// The sky (normal, and lit by lightning) and colour 31 down the screen,
// and every colour at every brightness, so a fade step is only a copy
static UWORD sky_lines[2][VIEW_H];
static UWORD shine_lines[VIEW_H];
static UWORD sky_faded[17][VIEW_H], shine_faded[17][VIEW_H];
static UWORD palette_faded[17][32];
static UBYTE scaled[17][16];    // scaled[b][v]: v (0-15) at brightness b

static UWORD rnd_state = 0x5eed;
static UWORD rnd(void) {
    rnd_state ^= rnd_state << 7;
    rnd_state ^= rnd_state >> 9;
    rnd_state ^= rnd_state << 8;
    return rnd_state;
}

static UWORD dim_to(UWORD c, UBYTE b) {
    const UBYTE *t = scaled[b];

    return t[(c >> 8) & 15] << 8 | t[(c >> 4) & 15] << 4 | t[c & 15];
}

static UWORD blend(UWORD a, UWORD b, UWORD t, UWORD n) {
    UWORD out = 0;

    for (UWORD s = 0; s < 12; s += 4) {
        int ca = (a >> s) & 15, cb = (b >> s) & 15;
        out |= (UWORD)(ca + (cb - ca) * (int)t / (int)n) << s;
    }
    return out;
}

// Three bands of gradient down the screen
static void gradient(UWORD *lines, const UWORD *keys, int top, int height) {
    for (int y = 0; y < height; y++) {
        UWORD band = y * 3 / height, at = y - band * height / 3;
        lines[top + y] = blend(keys[band], keys[band + 1], at, height / 3 + 1);
    }
}

static void prepare_fades(void) {
    for (UBYTE b = 0; b <= 16; b++) {
        for (UWORD y = 0; y < VIEW_H; y++) {
            sky_faded[b][y] = dim_to(sky_lines[0][y], b);
            shine_faded[b][y] = dim_to(shine_lines[y], b);
        }
        for (int i = 1; i < 32; i++) palette_faded[b][i] = dim_to(game_palette[i], b * theme->tint >> 4);
    }
}

static void apply_colours(void) {
    // A lightning strike flickers: lit, dark, lit, dark (only ever in play,
    // at full brightness)
    const UWORD *sky = (flash & 2) ? sky_lines[1] : sky_faded[brightness];
    const UWORD *shine = shine_faded[brightness];
    const UWORD *pal = palette_faded[(flash & 2) ? 16 : brightness];

    for (int i = 1; i < 32; i++) cop_colours[(i - 1) * 2 + 1] = pal[i];
    *cop_sky_top = sky[0];
    for (UWORD y = 0; y < VIEW_H; y++) {
        *cop_sky[y] = sky[y];
        *cop_shine[y] = shine[y];
    }
}

static void set_theme(const theme_t *t) {
    theme = t;
    flash = 0;
    next_strike = 120;
    gradient(sky_lines[0], theme->sky, 0, VIEW_H);
    gradient(sky_lines[1], flash_sky, 0, VIEW_H);
    for (UWORD y = 0; y < VIEW_H; y++) shine_lines[y] = game_palette[PAL_SHINE];
}

void video_theme(unsigned char level) {
    set_theme(&themes[level % THEME_COUNT]);
    prepare_fades();
    apply_colours();
}

int video_outdoors(void) {
    return theme->outdoor;
}

void video_theme_title(int logo_top, int logo_height) {
    set_theme(&title_theme);
    gradient(shine_lines, logo_shine, logo_top, logo_height);
    prepare_fades();
    apply_colours();
}

void video_brightness(unsigned char level) {
    brightness = level;
    apply_colours();
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

    worlds[0] = (UBYTE *)AllocMem(WORLD_SIZE, MEMF_CHIP | MEMF_CLEAR);
    worlds[1] = (UBYTE *)AllocMem(WORLD_SIZE, MEMF_CHIP | MEMF_CLEAR);
    copper = (UWORD *)AllocMem(COPPER_WORDS * 2, MEMF_CHIP | MEMF_CLEAR);
    // Room to save what's under the biggest enemy, for each one drawn
    ULONG save_words = 0;
    for (int i = 0; i < BOB_COUNT; i++) {
        ULONG w = (ULONG)bob_info[i].words * bob_info[i].height * PLANES;
        if (w > save_words) save_words = w;
    }
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < MAX_BOBS; i++) drawn[b][i].save = (UWORD *)AllocMem(save_words * 2, MEMF_CHIP);
    }
    front = 0;
    back = 1;
    for (int b = 0; b <= 16; b++) {
        for (int v = 0; v < 16; v++) scaled[b][v] = v * b >> 4;
    }

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
    for (int i = 1; i < 32; i++) c = move(c, REG(color[i]), 0);
    c = move(c, REG(diwstrt), (DISPLAY_TOP << 8) | 0x81);
    c = move(c, REG(diwstop), ((DISPLAY_TOP + VIEW_H + HUD_HEIGHT - 256) << 8) | 0xc1);
    c = move(c, REG(ddfstrt), 0x0030);
    c = move(c, REG(ddfstop), 0x00d0);
    c = move(c, REG(bplcon0), 0x5200);      // 5 planes, colour
    c = move(c, REG(bplcon2), 0x0024);      // sprites in front
    c = move(c, REG(bpl1mod), STRIDE - FETCH_BYTES);
    c = move(c, REG(bpl2mod), STRIDE - FETCH_BYTES);
    cop_bplcon1 = c + 1;
    c = move(c, REG(bplcon1), 0);
    cop_sprites = c;
    for (int i = 0; i < 8; i++) {
        c = move(c, REG(sprpt[i]), (UWORD)((ULONG)null_sprite >> 16));
        c = move(c, REG(sprpt[i]) + 2, (UWORD)(ULONG)null_sprite);
    }
    cop_planes = c;
    for (int i = 0; i < PLANES; i++) {
        c = move(c, REG(bplpt[i]), 0);
        c = move(c, REG(bplpt[i]) + 2, 0);
    }
    // The sky (colour 0) and colour 31, changed every line
    for (UWORD y = 0; y < VIEW_H; y++) {
        UWORD line = DISPLAY_TOP + y;
        if (line == 256) {
            *c++ = 0xffdf;          // past line 255: the wait wraps
            *c++ = 0xfffe;
        }
        c = wait_line(c, line);
        cop_sky[y] = c + 1;
        c = move(c, REG(color[0]), 0);
        cop_shine[y] = c + 1;
        c = move(c, REG(color[31]), 0);
    }
    // The HUD: its own little screen below. Switch in the right border of
    // the line before, so it's all set before the HUD's first line.
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
    memclr(worlds[0], WORLD_SIZE);
    memclr(worlds[1], WORLD_SIZE);
    drawn_count[0] = drawn_count[1] = 0;
}

static void draw_cell_in(UBYTE *world, unsigned char col, unsigned char row, unsigned char tile) {
    const UWORD *src = tile_gfx[tile];
    UBYTE *dst = world + (ULONG)row * 16 * STRIDE + 2 + col * 2;

    for (int y = 0; y < 16; y++, dst += STRIDE) {
        for (int p = 0; p < PLANES; p++) *(UWORD *)(dst + p * ROWBYTES) = *src++;
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
    at = (ULONG)y * STRIDE + ((x + 16) >> 3);
    bit = 0x80 >> ((x + 16) & 7);
    for (int w = 0; w < 2; w++) {
        for (int p = 0; p < PLANES; p++) {
            UBYTE *b = worlds[w] + at + p * ROWBYTES;
            *b = (colour & (1 << p)) ? (*b | bit) : (*b & ~bit);
        }
    }
}

// --------------------------------------------------------------- scroll

// The fetch starts a word early, so with no delay the screen's left edge
// shows bitmap pixel (pointer * 8 + 16). The world has 16 pixels of margin
// on the left, so pixel x of the picture is at bitmap x + 16.
void video_scroll(int cam_x, int cam_y) {
    UWORD delay = (16 - (cam_x & 15)) & 15;
    ULONG p = (ULONG)(worlds[front] + (ULONG)cam_y * STRIDE + ((cam_x + 15) >> 4) * 2);

    for (int i = 0; i < PLANES; i++) {
        ULONG a = p + i * ROWBYTES;
        cop_planes[i * 4 + 1] = (UWORD)(a >> 16);
        cop_planes[i * 4 + 3] = (UWORD)a;
    }
    *cop_bplcon1 = delay | (delay << 4);
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

// Put back what was under the enemies last drawn in this buffer, the last
// first, in case they overlapped
void video_bobs_begin(void) {
    for (int i = drawn_count[back] - 1; i >= 0; i--) {
        drawn_t *d = &drawn[back][i];
        wait_blit();
        custom->bltcon0 = SRCA | DEST | 0xf0;
        custom->bltcon1 = 0;
        custom->bltafwm = 0xffff;
        custom->bltalwm = 0xffff;
        custom->bltapt = d->save;
        custom->bltdpt = d->dst;
        custom->bltamod = 0;
        custom->bltdmod = ROWBYTES - d->words * 2;
        custom->bltsize = ((d->height * PLANES) << HSIZEBITS) | d->words;
    }
    drawn_count[back] = 0;
}

// Blitter: save what's there, then D = A ? B : C, where A is the mask, B
// the enemy and C the level underneath, all shifted right by x & 15. Each
// line of the bob is its width in words, for each of the 5 planes.
void video_bob(int x, int y, unsigned char frame) {
    const bob_t *b = &bob_info[frame];
    UWORD words = b->words, h = b->height, shift;
    drawn_t *d;
    UBYTE *dst;

    x += b->x;
    y += b->y;
    if (drawn_count[back] == MAX_BOBS || x < 0 || y < 0) return;
    if (x + words * 16 > WORLD_W + 16 || y + h > WORLD_H) return;
    shift = x & 15;
    dst = worlds[back] + (ULONG)y * STRIDE + 2 + (x >> 4) * 2;
    d = &drawn[back][drawn_count[back]++];
    d->dst = dst;
    d->words = words;
    d->height = h;

    wait_blit();
    custom->bltcon0 = SRCA | DEST | 0xf0;
    custom->bltcon1 = 0;
    custom->bltafwm = 0xffff;
    custom->bltalwm = 0xffff;
    custom->bltapt = dst;
    custom->bltdpt = d->save;
    custom->bltamod = ROWBYTES - words * 2;
    custom->bltdmod = 0;
    custom->bltsize = ((h * PLANES) << HSIZEBITS) | words;

    wait_blit();
    custom->bltcon0 = SRCA | SRCB | SRCC | DEST | 0xca | (shift << ASHIFTSHIFT);
    custom->bltcon1 = shift << BSHIFTSHIFT;
    custom->bltapt = (APTR)(bob_gfx + b->offset + (ULONG)words * PLANES * h);
    custom->bltbpt = (APTR)(bob_gfx + b->offset);
    custom->bltcpt = dst;
    custom->bltdpt = dst;
    custom->bltamod = 0;
    custom->bltbmod = 0;
    custom->bltcmod = ROWBYTES - words * 2;
    custom->bltdmod = ROWBYTES - words * 2;
    custom->bltsize = ((h * PLANES) << HSIZEBITS) | words;
}
void video_bobs_end(void) {
    UBYTE t = front;

    front = back;
    back = t;
}
