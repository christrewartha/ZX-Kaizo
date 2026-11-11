#include "render.h"

unsigned int screen_row[192];

sprite_t sprites[MAX_SPRITES];
unsigned char sprite_count;

#define SHIFT_BYTES 96          // 16 lines x {mask, gfx} x 3 bytes

static unsigned char spr_data[SPR_FIRST_EVEN * 8 * SHIFT_BYTES +
                              (SPR_FRAMES - SPR_FIRST_EVEN) * 4 * SHIFT_BYTES];

// Data for each frame at each x & 7. Half-shift frames point odd
// positions at the even shift below. render.asm draws from these.
unsigned char *spr_shift_ptr[SPR_FRAMES][8];

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
    sprites_reset();
}
