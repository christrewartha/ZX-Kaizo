#include <string.h>
#include "hud.h"
#include "render.h"

// The 48K ROM's character set: 8 bytes per character, from code 32
#define ROM_FONT ((const unsigned char *)0x3C00)

#define ATTR_NAME_BAR 0x4E      // bright yellow on blue
#define ATTR_STATUS   0x47      // bright white on black

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

void hud_room(const char *name) {
    memset(SCREEN_ATTR + HUD_ROW * 32, ATTR_NAME_BAR, 32);
    memset(SCREEN_ATTR + (HUD_ROW + 1) * 32, ATTR_STATUS, 32);
    // room_load() has already cleared the bitmap
    print_at((32 - strlen(name)) >> 1, HUD_ROW, name);
}
