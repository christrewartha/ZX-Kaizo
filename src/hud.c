#include <string.h>
#include "hud.h"
#include "render.h"
#include "room.h"

// The 48K ROM's character set: 8 bytes per character, from code 32
#define ROM_FONT ((const unsigned char *)0x3C00)

#define ATTR_NAME_BAR 0x4E      // bright yellow on blue
#define ATTR_STATUS   0x47      // bright white on black

#define DEATHS_COL    21        // "DEATHS 0000" ends in the last column
#define TROPHY_COL    13        // a cup, then "00/15"

#define ATTR_CUP_HELD  0xC6     // flashing bright yellow: carrying the trophy
#define ATTR_CUP_EMPTY 0x01     // dim blue: not

void print_at(unsigned char col, unsigned char row, const char *text) {
    unsigned char *scr = (unsigned char *)(screen_row[row << 3] + col);
    const unsigned char *glyph;
    unsigned char *p;
    unsigned char line;

    while (*text) {
        glyph = ROM_FONT + ((unsigned int)(unsigned char)*text++ << 3);
        p = scr++;
        for (line = 0; line < 8; line++) {
            *p = *glyph++;
            p += 256;
        }
    }
}

char *format_number(char *out, unsigned int n, unsigned char digits) {
    out[digits] = 0;
    while (digits) {
        out[--digits] = '0' + n % 10;
        n /= 10;
    }
    return out;
}

void hud_room(const char *name, unsigned char level, unsigned char count) {
    char num[3];

    memset(SCREEN_ATTR + HUD_ROW * 32, ATTR_NAME_BAR, 32);
    memset(SCREEN_ATTR + (HUD_ROW + 1) * 32, ATTR_STATUS, 32);
    // room_load() has already cleared the bitmap
    print_at((32 - strlen(name)) >> 1, HUD_ROW, name);
    print_at(0, HUD_ROW + 1, "LEVEL");
    print_at(6, HUD_ROW + 1, format_number(num, level, 2));
    print_at(8, HUD_ROW + 1, "/");
    print_at(9, HUD_ROW + 1, format_number(num, count, 2));
    print_at(DEATHS_COL, HUD_ROW + 1, "DEATHS");
}

void hud_trophies(unsigned char won, unsigned char count, unsigned char held) {
    char num[3];

    draw_trophy_at(TROPHY_COL, HUD_ROW + 1, 1);
    SCREEN_ATTR[(HUD_ROW + 1) * 32 + TROPHY_COL] = held ? ATTR_CUP_HELD : ATTR_CUP_EMPTY;
    print_at(TROPHY_COL + 1, HUD_ROW + 1, format_number(num, won, 2));
    print_at(TROPHY_COL + 3, HUD_ROW + 1, "/");
    print_at(TROPHY_COL + 4, HUD_ROW + 1, format_number(num, count, 2));
}

void hud_deaths(unsigned int deaths) {
    char num[5];

    if (deaths > 9999) deaths = 9999;
    print_at(DEATHS_COL + 7, HUD_ROW + 1, format_number(num, deaths, 4));
}
