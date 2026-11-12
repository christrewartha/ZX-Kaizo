#include <string.h>
#include "panel.h"
#include "gfx.h"
#include "hw.h"
#include "irq.h"
#include "charset_data.h"

#define NAME_ROW    (PANEL_ROW + 1)
#define STATUS_ROW  (PANEL_ROW + 2)
#define LEVEL_COL   2           // "LEVEL 01/15"
#define TROPHY_COL  16          // cup, "00/15"
#define DEATHS_COL  27          // skull, "DEATHS 0000"

#define NAME_COLOUR   VCOL_YELLOW       // text is hi-res: colours 0-7 only

static unsigned char name_col, name_len;
static unsigned char cup_held;

void panel_level(const char *name, unsigned char level, unsigned char count) {
    char num[3];
    unsigned int at = PANEL_ROW * SCREEN_COLS;

    // The ledge's top half is solid black: see [ledge] in chars.txt
    memset(SCREEN + at, CH_LEDGE, SCREEN_COLS);
    memset(COLOUR_RAM + at, 8 + VCOL_BLACK, SCREEN_COLS);
    memset(SCREEN + at + SCREEN_COLS, ' ', SCREEN_COLS * 2);

    name_len = strlen(name);
    name_col = (SCREEN_COLS - name_len) >> 1;
    print_at(name_col, NAME_ROW, name, NAME_COLOUR);

    print_at(LEVEL_COL, STATUS_ROW, "LEVEL", VCOL_WHITE);
    print_at(LEVEL_COL + 6, STATUS_ROW, format_number(num, level, 2), VCOL_WHITE);
    print_at(LEVEL_COL + 8, STATUS_ROW, "/", VCOL_WHITE);
    print_at(LEVEL_COL + 9, STATUS_ROW, format_number(num, count, 2), VCOL_WHITE);

    SCREEN[STATUS_ROW * SCREEN_COLS + DEATHS_COL] = CH_SKULL;
    COLOUR_RAM[STATUS_ROW * SCREEN_COLS + DEATHS_COL] = VCOL_WHITE;
    print_at(DEATHS_COL + 2, STATUS_ROW, "DEATHS", VCOL_WHITE);
}

void panel_deaths(unsigned int deaths) {
    char num[5];

    if (deaths > 9999) deaths = 9999;
    print_at(DEATHS_COL + 9, STATUS_ROW, format_number(num, deaths, 4), VCOL_WHITE);
}

void panel_trophies(unsigned char won, unsigned char count, unsigned char held) {
    char num[3];

    SCREEN[STATUS_ROW * SCREEN_COLS + TROPHY_COL] = CH_CUP;
    COLOUR_RAM[STATUS_ROW * SCREEN_COLS + TROPHY_COL] = held ? VCOL_YELLOW : VCOL_BLUE;
    print_at(TROPHY_COL + 2, STATUS_ROW, format_number(num, won, 2), VCOL_YELLOW);
    print_at(TROPHY_COL + 4, STATUS_ROW, "/", VCOL_YELLOW);
    print_at(TROPHY_COL + 5, STATUS_ROW, format_number(num, count, 2), VCOL_YELLOW);
    cup_held = held;
}

// The glint: white, fading through yellow
static const unsigned char glint[4] = { VCOL_WHITE, VCOL_WHITE, VCOL_YELLOW, VCOL_YELLOW };

void panel_animate(void) {
    unsigned char f = frame_count;
    unsigned char *colour = COLOUR_RAM + NAME_ROW * SCREEN_COLS + name_col;
    unsigned char i, pos;

    // Every 256 frames the glint crosses the name, a letter a frame
    pos = f - 0x80;
    if (pos < name_len + 4) {
        for (i = 0; i < name_len; i++) {
            colour[i] = (pos >= i && pos - i < 4) ? glint[pos - i] : NAME_COLOUR;
        }
    }

    if (cup_held) {
        COLOUR_RAM[STATUS_ROW * SCREEN_COLS + TROPHY_COL] = (f & 8) ? VCOL_WHITE : VCOL_YELLOW;
    }
}
