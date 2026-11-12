#include <string.h>
#include "screens.h"
#include "gfx.h"
#include "hw.h"
#include "irq.h"
#include "input.h"
#include "spr.h"
#include "sound.h"
#include "charset_data.h"
#include "sprite_data.h"

extern const signed char sine64[64];

#define LOGO_TEXT   "KAIZ64O"
#define LOGO_ROW    2
#define LOGO_ROWS   8
#define LOGO_COL    ((SCREEN_COLS - (sizeof(LOGO_TEXT) - 1) * 4) / 2)

#define SCROLL_ROW  23

// The logo's bars: dark and mid shared colours, and the washing top
#define LOGO_DARK   VCOL_RED
#define LOGO_MID    VCOL_ORANGE
static const unsigned char logo_wash[8] = {
    VCOL_YELLOW, VCOL_WHITE, VCOL_YELLOW, VCOL_YELLOW, VCOL_RED, VCOL_PURPLE, VCOL_RED, VCOL_YELLOW
};

// The scroller fades in and out at the edges
static const unsigned char scroll_fade[8] = {
    VCOL_BLUE, VCOL_BLUE, VCOL_PURPLE, VCOL_PURPLE, VCOL_CYAN, VCOL_CYAN, VCOL_WHITE, VCOL_WHITE
};

static const char scroll_text[] =
    "WELCOME TO KAIZ64O, A TOUGH AND PRECISE PLATFORMER FOR THE COMMODORE 64.   "
    "CLIMB AND WALL-JUMP THROUGH 15 LEVELS OF A HAUNTED CASTLE, FROM THE KITCHEN "
    "UP INTO THE EYE OF THE STORM, PAST SPIKES, BEETLES AND BATS.   "
    "HOLD FIRE LONGER TO JUMP HIGHER. PUSH INTO A WALL TO SLIDE DOWN IT, AND JUMP "
    "TO KICK OFF.  HOLD DOWN TO DROP THROUGH A PLATFORM.   "
    "EVERY LEVEL HIDES A TROPHY: GRAB IT AND CARRY IT TO THE EXIT TO WIN IT.   "
    "LIVES ARE UNLIMITED. YOUR PATIENCE MAY NOT BE...   GOOD LUCK!          ";

static const char *scroll_pos;
static signed char scroll_x;

// Draw a word in big letters: every 2 pixels of a font letter's row
// become one bar character, so each letter is 4 cells wide, 8 tall
static void draw_logo(const char *text, unsigned char col, unsigned char row) {
    static const unsigned char bar[4] = { ' ', CH_LOGO_R, CH_LOGO_L, CH_LOGO_LR };
    const unsigned char *glyph;
    unsigned char y, x, bits;
    unsigned int at;

    for (; *text; text++, col += 4) {
        glyph = CHARSET + screen_code(*text) * 8;
        for (y = 0; y < 8; y++) {
            bits = glyph[y];
            at = (row + y) * SCREEN_COLS + col;
            for (x = 0; x < 4; x++, bits <<= 2) {
                SCREEN[at + x] = bar[bits >> 6];
            }
        }
    }
}

static void wash_logo(unsigned char row) {
    unsigned char y, t = frame_count >> 2;

    for (y = 0; y < LOGO_ROWS; y++) {
        memset(COLOUR_RAM + (row + y) * SCREEN_COLS, 8 + logo_wash[(y + t) & 7], SCREEN_COLS);
    }
}

static void centre(unsigned char row, const char *text, unsigned char colour) {
    print_at((SCREEN_COLS - strlen(text)) >> 1, row, text, colour);
}

// Splits: the logo palette at the top, and the scroller's smooth scroll in
// 38-column mode (the rows either side of it are blank)
static void screen_splits(unsigned char scroller) {
    split_t *s = splits;

    s->line = FRAME_LINE;
    s->d011 = D011_NORMAL;
    s->d016 = D016_NORMAL;
    s->back = VCOL_BLACK;
    s->back1 = LOGO_DARK;
    s->back2 = LOGO_MID;
    s->border = VCOL_BLACK;
    split_count = 1;
    if (scroller) {
        splits[1] = *s;
        splits[1].line = ROW_LINE(SCROLL_ROW) - 3;
        splits[1].d016 = 0x10 | 7;
        splits[2] = *s;
        splits[2].line = ROW_LINE(SCROLL_ROW + 1);
        split_count = 3;
    }
}

static void scroller_start(void) {
    unsigned char i;

    scroll_pos = scroll_text;
    scroll_x = 7;
    memset(SCREEN + SCROLL_ROW * SCREEN_COLS, ' ', SCREEN_COLS);
    for (i = 0; i < SCREEN_COLS; i++) {
        COLOUR_RAM[SCROLL_ROW * SCREEN_COLS + i] =
            scroll_fade[i < 8 ? i : (i >= SCREEN_COLS - 8 ? SCREEN_COLS - 1 - i : 7)];
    }
}

// 2 pixels a frame
static void scroller_step(void) {
    unsigned char *row = SCREEN + SCROLL_ROW * SCREEN_COLS;
    unsigned char i;

    scroll_x -= 2;
    if (scroll_x < 0) {
        scroll_x += 8;
        // (oscar64 1.32's memmove() loses the row here)
        for (i = 0; i < SCREEN_COLS - 1; i++) row[i] = row[i + 1];
        row[SCREEN_COLS - 1] = screen_code(*scroll_pos++);
        if (!*scroll_pos) scroll_pos = scroll_text;
    }
    splits[1].d016 = 0x10 | scroll_x;
}

// The title's cast: the climber running to and fro under the logo, and
// two bats circling
static int hero_x;
static signed char hero_dir;

static void cast_start(void) {
    hero_x = -16;
    hero_dir = 1;
}

static void cast_step(void) {
    static const unsigned char run[4] = { SP_P_RUN_1, SP_P_RUN_2, SP_P_RUN_3, SP_P_RUN_4 };
    static const unsigned char flap[4] = { SP_FLYER_1, SP_FLYER_2, SP_FLYER_3, SP_FLYER_2 };
    unsigned char f = frame_count, img, left = hero_dir < 0;
    unsigned char i;

    hero_x += hero_dir;
    if (hero_x > 256) hero_dir = -1;
    else if (hero_x < -16) hero_dir = 1;
    // (Written as 'img + 2 + left' below, this crashes oscar64 1.32)
    img = run[((unsigned char)hero_x >> 2) & 3] + left;
    hw[SLOT_PLAYER].x = hw[SLOT_OUTLINE].x = hero_x;
    hw[SLOT_PLAYER].y = hw[SLOT_OUTLINE].y = (LOGO_ROW + LOGO_ROWS) * 8 - 2;
    hw[SLOT_PLAYER].image = img;
    hw[SLOT_OUTLINE].image = img + 2;
    hw[SLOT_PLAYER].colour = VCOL_CYAN;
    hw[SLOT_OUTLINE].colour = VCOL_BLACK;
    hw[SLOT_PLAYER].on = hw[SLOT_OUTLINE].on = 1;

    for (i = 0; i < 2; i++) {
        hwsprite_t *s = &hw[SLOT_ENEMY + i];
        unsigned char a = f + i * 32;

        s->x = (i ? 236 : 4) + (sine64[a & 63] >> 3);
        s->y = 40 + (sine64[(a * 2 + 16) & 63] >> 3);
        s->image = flap[((f >> 2) + i) & 3];
        s->colour = i ? VCOL_PURPLE : VCOL_LT_RED;
        s->on = 1;
    }
}

#define WAIT_SCROLLER  0x01
#define WAIT_CAST      0x02
#define WAIT_FIREWORKS 0x04

// Wait for a jump (let go of any held one first), animating meanwhile
static void wait_for_jump(unsigned char prompt_row, unsigned char what) {
    unsigned char held = 1;

    for (;;) {
        wait_frame();
        spr_commit(0);
        wash_logo(LOGO_ROW);
        if (what & WAIT_SCROLLER) scroller_step();
        if (what & WAIT_CAST) cast_step();
        if ((what & WAIT_FIREWORKS) && !(frame_count & 15)) {
            fx_spawn(FX_SPARK, (rnd() % 220) + 10, (rnd() & 63) + 8);
        }
        fx_update();
        memset(COLOUR_RAM + prompt_row * SCREEN_COLS, (frame_count & 16) ? VCOL_BLACK : VCOL_WHITE, SCREEN_COLS);

        if (input_read() & IN_JUMP) {
            if (!held) break;
        } else {
            held = 0;
        }
    }
    sound_play(SFX_START);
}

void title_screen(void) {
    sound_restart();
    spr_hide();
    fx_clear();
    screen_splits(1);
    gfx_clear();

    draw_logo(LOGO_TEXT, LOGO_COL, LOGO_ROW);
    centre(12, "A HAUNTED CASTLE. FIFTEEN LEVELS.", VCOL_WHITE);
    centre(13, "NO MERCY.", VCOL_RED);
    centre(15, "JOYSTICK IN EITHER PORT, OR KEYS:", VCOL_CYAN);
    centre(16, "O P  LEFT/RIGHT    Q SPACE M  JUMP", VCOL_WHITE);
    centre(17, "A  DROP THROUGH    N  SKIP LEVEL  ", VCOL_WHITE);
    centre(20, "PRESS FIRE TO START", VCOL_WHITE);
    scroller_start();
    cast_start();

    wait_for_jump(20, WAIT_SCROLLER | WAIT_CAST);
    spr_hide();
}

void ending_screen(unsigned int deaths, unsigned char trophies, unsigned char count) {
    char num[5];

    spr_hide();
    fx_clear();
    screen_splits(0);
    gfx_clear();

    draw_logo(LOGO_TEXT, LOGO_COL, LOGO_ROW);
    centre(12, "YOU HAVE ESCAPED THE CASTLE!", VCOL_YELLOW);
    print_at(14, 15, "DEATHS", VCOL_WHITE);
    print_at(22, 15, format_number(num, deaths > 9999 ? 9999 : deaths, 4), VCOL_WHITE);
    print_at(14, 17, "TROPHIES", VCOL_YELLOW);
    print_at(23, 17, format_number(num, trophies, 2), VCOL_YELLOW);
    print_at(25, 17, "/", VCOL_YELLOW);
    print_at(26, 17, format_number(num, count, 2), VCOL_YELLOW);
    if (trophies == count) centre(19, "EVERY TROPHY! A TRUE MASTER.", VCOL_CYAN);
    centre(22, "PRESS FIRE", VCOL_WHITE);

    wait_for_jump(22, WAIT_FIREWORKS);
    spr_hide();
}
