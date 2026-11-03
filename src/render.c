#include "render.h"

unsigned int screen_row[192];

sprite_t sprites[MAX_SPRITES];
unsigned char sprite_count;

// Parameters for the blitter in render.asm
unsigned char *spr_scr;
unsigned char *spr_gfx;
unsigned char *spr_save;

extern void spr_draw(void);
extern void spr_erase(void);

#define SHIFT_BYTES 96          // 16 lines x {mask, gfx} x 3 bytes

static unsigned char spr_data[SPR_FIRST_EVEN * 8 * SHIFT_BYTES +
                              (SPR_FRAMES - SPR_FIRST_EVEN) * 4 * SHIFT_BYTES];

// Data for each frame at each x & 7. Half-shift frames point odd
// positions at the even shift below.
static unsigned char *spr_shift_ptr[SPR_FRAMES][8];

// What was drawn, in draw order, so it can be undone in reverse: sprites
// that overlap restore each other's backgrounds correctly that way.
typedef struct {
    unsigned char *scr;
    unsigned char save[48];
    unsigned char *attr;        // NULL if this sprite didn't paint attributes
    unsigned char attr_save[9];
    unsigned char attr_rows, attr_cols;
} slot_t;

static slot_t slots[MAX_SPRITES];
static unsigned char drawn_count;

static unsigned int mirror16(unsigned int v) {
    unsigned int r = 0;
    unsigned char i;

    for (i = 0; i < 16; i++) {
        r = (r << 1) | (v & 1);
        v >>= 1;
    }
    return r;
}

// Build one frame (mirrored if asked) with a 1 pixel outline as the mask so
// the sprite stays readable over any background, then pre-shift it.
static unsigned char *build_frame(unsigned char f, unsigned char *out) {
    unsigned int gfx[16];
    unsigned int outline;
    unsigned long g, m;
    unsigned char art = sprite_frame_art[f];
    unsigned char step = (f >= SPR_FIRST_EVEN) ? 2 : 1;
    unsigned char row, shift;
    const unsigned int *src = sprite_art[art & ~ART_MIRROR];

    for (row = 0; row < 16; row++) {
        gfx[row] = (art & ART_MIRROR) ? mirror16(src[row]) : src[row];
    }

    for (shift = 0; shift < 8; shift += step) {
        spr_shift_ptr[f][shift] = out;
        spr_shift_ptr[f][shift + step - 1] = out;
        for (row = 0; row < 16; row++) {
            outline = gfx[row] | (gfx[row] << 1) | (gfx[row] >> 1);
            if (row > 0)  outline |= gfx[row - 1];
            if (row < 15) outline |= gfx[row + 1];

            g = ((unsigned long)gfx[row] << 8) >> shift;
            m = ~(((unsigned long)outline << 8) >> shift);

            *out++ = (unsigned char)(m >> 16);
            *out++ = (unsigned char)(g >> 16);
            *out++ = (unsigned char)(m >> 8);
            *out++ = (unsigned char)(g >> 8);
            *out++ = (unsigned char)m;
            *out++ = (unsigned char)g;
        }
    }
    return out;
}

void render_init(void) {
    unsigned char *out = spr_data;
    unsigned char y, f;

    // Spectrum line address: 010T TLLL RRRC CCCC (T = third, L = line in cell, R = cell row)
    y = 0;
    do {
        screen_row[y] = 0x4000 | ((unsigned int)(y & 0xC0) << 5) | ((unsigned int)(y & 0x07) << 8) | ((y & 0x38) << 2);
    } while (++y < 192);

    for (f = 0; f < SPR_FRAMES; f++) {
        out = build_frame(f, out);
    }
    drawn_count = 0;
}

// Colour only the cells the sprite actually covers: 2x2 when it is cell
// aligned, up to 3x3 when it isn't. Paper is kept, ink is replaced.
static void attr_paint(slot_t *s, unsigned char x, unsigned char y, unsigned char colour) {
    unsigned char *a = SCREEN_ATTR + ((unsigned int)(y & 0xF8) << 2) + (x >> 3);
    unsigned char *save = s->attr_save;
    unsigned char rows = (y & 7) ? 3 : 2;
    unsigned char cols = (x & 7) ? 3 : 2;
    unsigned char c;

    s->attr = a;
    s->attr_rows = rows;
    s->attr_cols = cols;
    do {
        c = cols;
        do {
            *save++ = *a;
            *a = (*a & 0x38) | colour;
            a++;
        } while (--c);
        a += 32 - cols;
    } while (--rows);
}

static void attr_restore(slot_t *s) {
    unsigned char *a = s->attr;
    unsigned char *save = s->attr_save;
    unsigned char rows = s->attr_rows;
    unsigned char cols = s->attr_cols;
    unsigned char c;

    do {
        c = cols;
        do {
            *a++ = *save++;
        } while (--c);
        a += 32 - cols;
    } while (--rows);
}

void sprites_render(void) {
    unsigned char order[MAX_SPRITES];
    unsigned char n, i, j, y;
    slot_t *s;
    sprite_t *sp;

    // Undo last frame in reverse draw order
    s = &slots[drawn_count];
    while (drawn_count) {
        --s;
        --drawn_count;
        spr_scr = s->scr;
        spr_save = s->save;
        spr_erase();
        if (s->attr) attr_restore(s);
    }

    // Draw top to bottom so we stay ahead of the beam. Insertion sort: a
    // handful of sprites, nearly always already in order.
    n = 0;
    for (i = 0; i < sprite_count; i++) {
        if (!sprites[i].visible) continue;
        y = sprites[i].y;
        for (j = n; j && sprites[order[j - 1]].y > y; j--) {
            order[j] = order[j - 1];
        }
        order[j] = i;
        n++;
    }

    s = slots;
    for (i = 0; i < n; i++, s++) {
        sp = &sprites[order[i]];
        if (sp->colour) {
            attr_paint(s, sp->x, sp->y, sp->colour);
        } else {
            s->attr = 0;
        }
        s->scr = (unsigned char *)(screen_row[sp->y] + (sp->x >> 3));
        spr_scr = s->scr;
        spr_gfx = spr_shift_ptr[sp->frame][sp->x & 7];
        spr_save = s->save;
        spr_draw();
    }
    drawn_count = n;
}

void sprites_reset(void) {
    drawn_count = 0;
}
