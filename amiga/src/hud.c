#include "gcc8_c_support.h"
#include <proto/exec.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include "hud.h"
#include "hw.h"

extern struct GfxBase *GfxBase;

#define REG(r)      ((UWORD)offsetof(struct Custom, r))

#define ROWBYTES    40
#define STRIDE      (ROWBYTES * 2)      // 2 planes, interleaved

#define NAME_Y      4
#define STATUS_Y    14
#define LEVEL_COL   1                   // "LEVEL 01/15"
#define CUP_COL     15                  // cup, "00/15"
#define DEATHS_COL  28                  // "DEATHS 0000"

// Colours: background, text, highlight, rule
#define C_TEXT      1
#define C_GOLD      2
#define C_RULE      3
static const UWORD colours[4] = { 0x112, 0xccd, 0xfc4, 0x445 };

static UBYTE *bar;
static UBYTE font[96][8];               // characters 32-127, 8x8
static UBYTE cup_held;

// The cup, as an 8x8 character
static const UBYTE cup[8] = { 0xff, 0x7e, 0x7e, 0x3c, 0x18, 0x18, 0x3c, 0x7e };

void hud_init(void) {
    struct TextFont *tf = GfxBase->DefaultFont;
    const UBYTE *data = (const UBYTE *)tf->tf_CharData;
    const UWORD *loc = (const UWORD *)tf->tf_CharLoc;

    bar = (UBYTE *)AllocMem(STRIDE * HUD_HEIGHT, MEMF_CHIP | MEMF_CLEAR);

    // Each character's bits are at an offset along the font's rows
    for (int ch = 32; ch < 128; ch++) {
        if (ch < tf->tf_LoChar || ch > tf->tf_HiChar || tf->tf_YSize < 8) continue;
        UWORD offset = loc[(ch - tf->tf_LoChar) * 2], width = loc[(ch - tf->tf_LoChar) * 2 + 1];
        for (int y = 0; y < 8; y++) {
            const UBYTE *row = data + y * tf->tf_Modulo;
            UBYTE out = 0;
            for (UWORD b = 0; b < width && b < 8; b++) {
                UWORD bit = offset + b;
                if (row[bit >> 3] & (0x80 >> (bit & 7))) out |= 0x80 >> b;
            }
            font[ch - 32][y] = out;
        }
    }

    // The rule across the top: 2 lines in colour 3 (both planes)
    for (int i = 0; i < STRIDE * 2; i++) bar[i] = 0xff;
}

UWORD *hud_copper(UWORD *c) {
    ULONG p = (ULONG)bar;

    *c++ = REG(bplcon0); *c++ = 0x2200;     // 2 planes, one playfield
    *c++ = REG(bplcon1); *c++ = 0;
    *c++ = REG(ddfstrt); *c++ = 0x0038;     // no scroll: the usual fetch
    *c++ = REG(bpl1mod); *c++ = STRIDE - ROWBYTES;
    *c++ = REG(bpl2mod); *c++ = STRIDE - ROWBYTES;
    *c++ = REG(bplpt[0]); *c++ = (UWORD)(p >> 16);
    *c++ = REG(bplpt[0]) + 2; *c++ = (UWORD)p;
    *c++ = REG(bplpt[1]); *c++ = (UWORD)((p + ROWBYTES) >> 16);
    *c++ = REG(bplpt[1]) + 2; *c++ = (UWORD)(p + ROWBYTES);
    for (int i = 0; i < 4; i++) {
        *c++ = REG(color[i]);
        *c++ = colours[i];
    }
    return c;
}

static void put_char(int col, int y, const UBYTE *glyph, UBYTE colour) {
    UBYTE *p = bar + y * STRIDE + col;

    for (int r = 0; r < 8; r++, p += STRIDE) {
        p[0] = (colour & 1) ? glyph[r] : 0;
        p[ROWBYTES] = (colour & 2) ? glyph[r] : 0;
    }
}

static void print(int col, int y, const char *text, UBYTE colour) {
    for (; *text && col < ROWBYTES; text++, col++) {
        unsigned char ch = *text;
        put_char(col, y, font[(ch >= 32 && ch < 128) ? ch - 32 : 0], colour);
    }
}

static char *number(char *out, unsigned int n, int digits) {
    out[digits] = 0;
    while (digits) {
        out[--digits] = '0' + n % 10;
        n /= 10;
    }
    return out;
}

void hud_level(const char *name, unsigned char level, unsigned char count) {
    char num[3];
    int len = strlen(name);

    // Clear the text rows
    memclr(bar + NAME_Y * STRIDE, (HUD_HEIGHT - NAME_Y) * STRIDE);
    print((ROWBYTES - len) / 2, NAME_Y, name, C_GOLD);
    print(LEVEL_COL, STATUS_Y, "LEVEL", C_TEXT);
    print(LEVEL_COL + 6, STATUS_Y, number(num, level, 2), C_TEXT);
    print(LEVEL_COL + 8, STATUS_Y, "/", C_TEXT);
    print(LEVEL_COL + 9, STATUS_Y, number(num, count, 2), C_TEXT);
    print(DEATHS_COL, STATUS_Y, "DEATHS", C_TEXT);
}

void hud_message(const char *top, const char *bottom) {
    memclr(bar + NAME_Y * STRIDE, (HUD_HEIGHT - NAME_Y) * STRIDE);
    print((ROWBYTES - strlen(top)) / 2, NAME_Y, top, C_GOLD);
    print((ROWBYTES - strlen(bottom)) / 2, STATUS_Y, bottom, C_TEXT);
    cup_held = 0;
}

const unsigned char *hud_glyph(char c) {
    unsigned char ch = c;

    return font[(ch >= 32 && ch < 128) ? ch - 32 : 0];
}

void hud_deaths(unsigned int deaths) {
    char num[5];

    print(DEATHS_COL + 7, STATUS_Y, number(num, deaths > 9999 ? 9999 : deaths, 4), C_TEXT);
}

void hud_trophies(unsigned char won, unsigned char count, unsigned char held) {
    char num[3];

    put_char(CUP_COL, STATUS_Y, cup, held ? C_GOLD : C_RULE);
    print(CUP_COL + 2, STATUS_Y, number(num, won, 2), C_GOLD);
    print(CUP_COL + 4, STATUS_Y, "/", C_GOLD);
    print(CUP_COL + 5, STATUS_Y, number(num, count, 2), C_GOLD);
    cup_held = held;
}

void hud_animate(void) {
    if (cup_held) put_char(CUP_COL, STATUS_Y, cup, (frame_count & 8) ? C_TEXT : C_GOLD);
}
