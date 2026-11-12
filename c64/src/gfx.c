#include <string.h>
#include "gfx.h"
#include "hw.h"
#include "irq.h"
#include "room.h"
#include "sound.h"
#include "charset_data.h"

// Tile ids, as produced by tools/build_data.ps1 (see src/room.c)
#define T_EMPTY      0
#define T_BRICK      1
#define T_BLOCK      2
#define T_PLATFORM   3
#define T_SPIKE_UP   4
#define T_SPIKE_DOWN 5
#define T_EXIT       6
#define T_TROPHY     7

static const unsigned char tile_coll[8] = {
    0, TILE_SOLID, TILE_SOLID, TILE_PLATFORM, TILE_HAZARD, TILE_HAZARD, TILE_EXIT, TILE_TROPHY
};

// Characters made at run time, above the ones from chars.txt
#define CH_PLAT_L    240        // the plank over each half of the indoor
#define CH_PLAT_R    241        // background stonework
#define CH_FLAME     242        // the torches' current frame

// Colour RAM: 8 + colour makes a cell multicolour
#define MC           8

#define THEME_OUTDOOR 0x01      // sky and stars instead of stonework
#define THEME_STORM   0x02      // lightning

typedef struct {
    unsigned char back, dark, mid;          // $D021-$D023
    unsigned char brick, block, plat, spike; // each tile's own colour, 0-7
    unsigned char flags;
} theme_t;

// One per level, in world.txt order: down in the castle to begin with,
// then out on the towers, and up into the storm
static const theme_t themes[] = {
    // The Kitchen: warm brick
    { VCOL_BLACK, VCOL_BROWN,     VCOL_ORANGE,   VCOL_YELLOW, VCOL_WHITE,  VCOL_YELLOW, VCOL_WHITE,  0 },
    // The Cellar Passage
    { VCOL_BLACK, VCOL_DARK_GREY, VCOL_BROWN,    VCOL_RED,    VCOL_YELLOW, VCOL_YELLOW, VCOL_WHITE,  0 },
    // The Crypt: cold stone and moss
    { VCOL_BLACK, VCOL_DARK_GREY, VCOL_MED_GREY, VCOL_CYAN,   VCOL_WHITE,  VCOL_YELLOW, VCOL_WHITE,  0 },
    // The Wine Cellar
    { VCOL_BLACK, VCOL_BROWN,     VCOL_RED,      VCOL_PURPLE, VCOL_YELLOW, VCOL_YELLOW, VCOL_WHITE,  0 },
    // The Chimney Stack: soot and embers
    { VCOL_BLACK, VCOL_DARK_GREY, VCOL_BROWN,    VCOL_RED,    VCOL_YELLOW, VCOL_YELLOW, VCOL_YELLOW, 0 },
    // The Belfry
    { VCOL_BLACK, VCOL_BROWN,     VCOL_MED_GREY, VCOL_WHITE,  VCOL_YELLOW, VCOL_YELLOW, VCOL_WHITE,  0 },
    // The Crow's Nest: out under the night sky
    { VCOL_BLUE,  VCOL_DARK_GREY, VCOL_MED_GREY, VCOL_WHITE,  VCOL_CYAN,   VCOL_YELLOW, VCOL_WHITE,  THEME_OUTDOOR },
    // Rooftops
    { VCOL_BLUE,  VCOL_BROWN,     VCOL_LT_RED,   VCOL_YELLOW, VCOL_WHITE,  VCOL_YELLOW, VCOL_WHITE,  THEME_OUTDOOR },
    // The Clock Tower
    { VCOL_BLUE,  VCOL_DARK_GREY, VCOL_LT_GREY,  VCOL_WHITE,  VCOL_YELLOW, VCOL_YELLOW, VCOL_CYAN,   THEME_OUTDOOR },
    // The Battlements
    { VCOL_BLUE,  VCOL_DARK_GREY, VCOL_MED_GREY, VCOL_CYAN,   VCOL_WHITE,  VCOL_YELLOW, VCOL_WHITE,  THEME_OUTDOOR },
    // The Gargoyle Ledge: clouds close in
    { VCOL_BLACK, VCOL_DARK_GREY, VCOL_MED_GREY, VCOL_GREEN,  VCOL_WHITE,  VCOL_YELLOW, VCOL_WHITE,  THEME_OUTDOOR },
    // The Spire: the storm begins
    { VCOL_BLACK, VCOL_BLUE,      VCOL_LT_BLUE,  VCOL_CYAN,   VCOL_WHITE,  VCOL_YELLOW, VCOL_WHITE,  THEME_OUTDOOR | THEME_STORM },
    // The Weathervane
    { VCOL_BLACK, VCOL_DARK_GREY, VCOL_LT_GREY,  VCOL_YELLOW, VCOL_CYAN,   VCOL_YELLOW, VCOL_WHITE,  THEME_OUTDOOR | THEME_STORM },
    // The Lightning Rod
    { VCOL_BLACK, VCOL_BLUE,      VCOL_LT_BLUE,  VCOL_WHITE,  VCOL_YELLOW, VCOL_YELLOW, VCOL_CYAN,   THEME_OUTDOOR | THEME_STORM },
    // Eye of the Storm
    { VCOL_BLACK, VCOL_PURPLE,    VCOL_LT_BLUE,  VCOL_CYAN,   VCOL_WHITE,  VCOL_YELLOW, VCOL_WHITE,  THEME_OUTDOOR | THEME_STORM },
};
#define THEME_COUNT (sizeof(themes) / sizeof(themes[0]))

// The HUD's palette, below the play area
#define HUD_BACK  VCOL_BLACK
#define HUD_DARK  VCOL_DARK_GREY
#define HUD_MID   VCOL_MED_GREY

unsigned char room_coll[(ROOM_ROWS + 2) * 32];
unsigned char room_current;
unsigned int room_trophy;

unsigned char gfx_shake_x;
unsigned char gfx_storm;

static const theme_t *theme;

// The next room, built here and then wiped onto the screen
static unsigned char tiles[ROOM_ROWS * 32];
static unsigned char next_chars[ROOM_ROWS * SCREEN_COLS];
static unsigned char next_colours[ROOM_ROWS * SCREEN_COLS];

// Cells that animate
#define MAX_EXITS  12
#define MAX_STARS  32
#define MAX_TORCHES 4
static unsigned int exit_cells[MAX_EXITS];
static unsigned char exit_count;
static unsigned int star_cells[MAX_STARS];
static unsigned char star_count;
static unsigned int torch_cells[MAX_TORCHES];
static unsigned char torch_count;

static unsigned char shake;
static unsigned char flash;             // lightning: frames of flash left
static unsigned char thunder;           // frames until the thunder
static unsigned char next_strike;       // frames until the next lightning

static unsigned int rnd_state = 0xACE1;

unsigned char rnd(void) {
    // 16-bit xorshift
    rnd_state ^= rnd_state << 7;
    rnd_state ^= rnd_state >> 9;
    rnd_state ^= rnd_state << 8;
    return (unsigned char)rnd_state;
}

// ------------------------------------------------------------------ text

unsigned char screen_code(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;   // our lower case, 65-90
    if (c >= '@' && c <= '_') return c - 64;    // ROM codes 0-31
    return c;                                   // space, digits, punctuation
}

void print_at(unsigned char col, unsigned char row, const char *text, unsigned char colour) {
    unsigned int at = row * SCREEN_COLS + col;

    while (*text) {
        SCREEN[at] = screen_code(*text++);
        COLOUR_RAM[at] = colour;
        at++;
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

// ------------------------------------------------------------ set-up

// The ROM font in codes 0-63 (upper case, digits, punctuation), and the
// second ROM set's lower case letters in 65-90
static void copy_font(void) {
    unsigned int i;

    __asm { sei }
    POKE(0x01, 0x33);           // character ROM in at $D000 instead of I/O
    for (i = 0; i < 64 * 8; i++) CHARSET[i] = PEEK(0xD000 + i);
    for (i = 8; i < 27 * 8; i++) CHARSET[64 * 8 + i] = PEEK(0xD800 + i);
    POKE(0x01, 0x37);
    __asm { cli }
}

void gfx_init(void) {
    vic.ctrl1 = 0x0B;           // screen off while setting up
    POKE(0xDD02, PEEK(0xDD02) | 0x03);
    POKE(CIA2_PRA, PEEK(CIA2_PRA) & 0xFC);      // VIC bank 3
    vic.memptr = 0x02;          // screen at +$0000, characters at +$0800
    vic.ctrl2 = D016_NORMAL;
    vic.color_border = VCOL_BLACK;

    memset(CHARSET, 0, 2048);
    copy_font();
    memcpy(CHARSET + CH_BASE * 8, charset_data, CH_DATA_COUNT * 8);
    gfx_clear();
}

void gfx_clear(void) {
    memset(SCREEN, ' ', 1000);
    memset(COLOUR_RAM, VCOL_BLACK, 1000);
}

// Mix a tile over a background character: the tile's pixels where it has
// them, the background's elsewhere (multicolour pixel pairs)
static void overlay_char(unsigned char dest, unsigned char tile, unsigned char back) {
    unsigned char *d = CHARSET + dest * 8;
    const unsigned char *t = CHARSET + tile * 8;
    const unsigned char *b = CHARSET + back * 8;
    unsigned char i, px, mask, out;

    for (i = 0; i < 8; i++) {
        out = 0;
        for (mask = 0xC0; mask; mask >>= 2) {
            px = t[i] & mask;
            out |= px ? px : (b[i] & mask);
        }
        d[i] = out;
    }
}

void gfx_game_splits(void) {
    splits[0].line = FRAME_LINE;
    splits[0].d011 = D011_NORMAL;
    splits[0].d016 = D016_NORMAL;
    splits[0].back = theme ? theme->back : VCOL_BLACK;
    splits[0].back1 = theme ? theme->dark : HUD_DARK;
    splits[0].back2 = theme ? theme->mid : HUD_MID;
    splits[0].border = VCOL_BLACK;
    // The HUD, from the top of its separator row
    splits[1].line = ROW_LINE(22);
    splits[1].d016 = D016_NORMAL;
    splits[1].back = HUD_BACK;
    splits[1].back1 = HUD_DARK;
    splits[1].back2 = HUD_MID;
    splits[1].border = VCOL_BLACK;
    split_count = 2;
}

// ---------------------------------------------------------------- rooms

static unsigned char is_solid(unsigned char t) {
    return t == T_BRICK || t == T_BLOCK;
}

// A fixed scatter for the background's variety, the same every visit
static unsigned char cell_hash(unsigned char r, unsigned char c) {
    unsigned char h = r * 37 + c * 11 + room_current * 101;

    h ^= h >> 3;
    return h * 29 + (c ^ r);
}

static void build_cell(unsigned char r, unsigned char c, unsigned char *ch, unsigned char *colour) {
    unsigned char t = tiles[r * 32 + c];
    unsigned char h = cell_hash(r, c);
    unsigned char left_half = ((r + c) & 1) == 0;
    unsigned char outdoor = theme->flags & THEME_OUTDOOR;

    switch (t) {
        case T_BRICK:
            *ch = (r > 0 && !is_solid(tiles[(r - 1) * 32 + c])) ? CH_BRICK_TOP : CH_BRICK;
            *colour = MC + theme->brick;
            return;
        case T_BLOCK:
            *ch = (h & 3) == 0 ? CH_BLOCK_RIVET : CH_BLOCK;
            *colour = MC + theme->block;
            return;
        case T_PLATFORM:
            *ch = outdoor ? CH_PLAT : (left_half ? CH_PLAT_L : CH_PLAT_R);
            *colour = MC + theme->plat;
            return;
        case T_SPIKE_UP:
            *ch = CH_SPIKE_UP;
            *colour = theme->spike;
            return;
        case T_SPIKE_DOWN:
            *ch = CH_SPIKE_DOWN;
            *colour = theme->spike;
            return;
        case T_EXIT:
            *ch = CH_EXIT;
            *colour = MC + VCOL_YELLOW;
            if (exit_count < MAX_EXITS) exit_cells[exit_count++] = r * SCREEN_COLS + PLAY_COL + c;
            return;
    }

    // Open space (the trophy is a sprite, over this)
    if (outdoor) {
        *ch = CH_SKY;
        *colour = MC + VCOL_WHITE;
        if (h < 14 && r < ROOM_ROWS - 4 && star_count < MAX_STARS) {
            *ch = (h & 3) ? CH_STAR_SMALL : CH_STAR_BIG;
            star_cells[star_count++] = r * SCREEN_COLS + PLAY_COL + c;
        }
    } else {
        *ch = left_half ? CH_BG_L : CH_BG_R;
        *colour = MC + VCOL_GREEN;
        if (h < 12) *ch = left_half ? CH_BG_L_CRACK : CH_BG_R_CRACK;
        else if (h > 248 && left_half) *ch = CH_BG_L_MOSS;
    }
}

// The castle walls either side, with a torch on each now and then
static void build_walls(void) {
    unsigned char r, c, ch, colour;
    unsigned int at;

    torch_count = 0;
    for (r = 0; r < ROOM_ROWS; r++) {
        for (c = 0; c < WALL_COLS; c++) {
            at = r * SCREEN_COLS;
            ch = CH_WALL;
            colour = MC + VCOL_BLACK;
            if (c == WALL_COLS - 1) ch = CH_WALL_EDGE_L;
            next_chars[at + c] = ch;
            next_colours[at + c] = colour;
            ch = (c == 0) ? CH_WALL_EDGE_R : CH_WALL;
            next_chars[at + SCREEN_COLS - WALL_COLS + c] = ch;
            next_colours[at + SCREEN_COLS - WALL_COLS + c] = colour;
        }
    }
    for (r = 6; r < ROOM_ROWS - 1; r += 9) {
        for (c = 1; c < SCREEN_COLS; c += SCREEN_COLS - 3) {
            at = r * SCREEN_COLS + c;
            next_chars[at] = CH_FLAME;
            next_colours[at] = VCOL_YELLOW;
            next_chars[at + SCREEN_COLS] = CH_SCONCE;
            next_colours[at + SCREEN_COLS] = MC + VCOL_YELLOW;
            torch_cells[torch_count++] = at;
        }
    }
}

static void build_room(unsigned char n) {
    const unsigned char *src = rooms[n].map;
    unsigned char *t = tiles;
    unsigned char run, v, r, c;
    unsigned int i;

    room_current = n;
    theme = &themes[n % THEME_COUNT];
    gfx_storm = theme->flags & THEME_STORM;

    // Unpack the run-length encoded map: each byte is (tile << 5) | (run - 1)
    while (t < tiles + ROOM_ROWS * 32) {
        v = *src >> 5;
        run = (*src++ & 31) + 1;
        do {
            *t++ = v;
        } while (--run);
    }

    exit_count = 0;
    star_count = 0;
    build_walls();
    for (r = 0; r < ROOM_ROWS; r++) {
        for (c = 0; c < 32; c++) {
            i = r * SCREEN_COLS + PLAY_COL + c;
            build_cell(r, c, &next_chars[i], &next_colours[i]);
        }
    }

    // Collision flags, plus two empty rows below for probes past the bottom
    room_trophy = NO_TROPHY;
    for (i = 0; i < ROOM_ROWS * 32; i++) {
        if (tiles[i] == T_TROPHY) room_trophy = i;
        room_coll[i] = tile_coll[tiles[i]];
    }
    memset(room_coll + ROOM_ROWS * 32, 0, 2 * 32);
}

// Planks over the indoor stonework, in this level's style
static void make_room_chars(void) {
    if (theme->flags & THEME_OUTDOOR) return;
    overlay_char(CH_PLAT_L, CH_PLAT, CH_BG_L);
    overlay_char(CH_PLAT_R, CH_PLAT, CH_BG_R);
}

// Wipe the play area diagonally, from the bottom-left, a few diagonals a
// frame: to black first, or (cover = 0) to the next room
#define WIPE_DIAGONALS (ROOM_ROWS + SCREEN_COLS - 1)
#define WIPE_STEP      4

static void wipe(unsigned char cover) {
    unsigned char d, r, c, first, last, k;
    int lo, hi;
    unsigned int at;

    for (d = 0; d < WIPE_DIAGONALS; d += WIPE_STEP) {
        wait_frame();
        for (r = 0; r < ROOM_ROWS; r++) {
            // Cells on diagonals d..d+WIPE_STEP-1: c + (ROOM_ROWS - 1 - r)
            lo = (int)d - (ROOM_ROWS - 1 - r);
            hi = lo + WIPE_STEP - 1;
            if (hi < 0 || lo >= SCREEN_COLS) continue;
            first = lo < 0 ? 0 : lo;
            last = hi >= SCREEN_COLS ? SCREEN_COLS - 1 : hi;
            at = r * SCREEN_COLS;
            for (k = first; k <= last; k++) {
                if (cover) {
                    SCREEN[at + k] = CH_SOLID;
                    COLOUR_RAM[at + k] = MC + VCOL_BLACK;
                } else {
                    SCREEN[at + k] = next_chars[at + k];
                    COLOUR_RAM[at + k] = next_colours[at + k];
                }
            }
        }
    }
}

void room_load(unsigned char n) {
    wipe(1);
    build_room(n);
    make_room_chars();
    gfx_game_splits();
    shake = 0;
    flash = 0;
    thunder = 0;
    next_strike = 100;
    wipe(0);
}

void room_set_trophy(unsigned char present) {
    if (room_trophy != NO_TROPHY) room_coll[room_trophy] = present ? TILE_TROPHY : 0;
}

// ------------------------------------------------------------ animation

static const unsigned char exit_wash[8] = {
    VCOL_YELLOW, VCOL_YELLOW, VCOL_WHITE, VCOL_YELLOW, VCOL_GREEN, VCOL_CYAN, VCOL_WHITE, VCOL_YELLOW
};
static const unsigned char star_colours[4] = { VCOL_WHITE, VCOL_CYAN, VCOL_BLUE, VCOL_WHITE };
static const unsigned char flame_colours[4] = { VCOL_YELLOW, VCOL_YELLOW, VCOL_WHITE, VCOL_RED };

void gfx_shake(unsigned char frames) {
    shake = frames;
}

void gfx_animate(void) {
    unsigned char f = frame_count;
    unsigned char i, b, back;
    unsigned char *p;
    const unsigned char *src;

    // The exit's chevrons march right, one double pixel every 2 frames,
    // while colours wash down it
    if (!(f & 1)) {
        p = CHARSET + CH_EXIT * 8;
        for (i = 0; i < 8; i++) {
            b = p[i];
            p[i] = (b >> 2) | (b << 6);
        }
    }
    for (i = 0; i < exit_count; i++) {
        COLOUR_RAM[exit_cells[i]] = MC + exit_wash[((f >> 2) - (unsigned char)(exit_cells[i] / SCREEN_COLS)) & 7];
    }

    // Torches flicker
    if (!(f & 3)) {
        src = charset_data + (CH_FLAME_0 - CH_BASE + rnd() % 3) * 8;
        memcpy(CHARSET + CH_FLAME * 8, src, 8);
        for (i = 0; i < torch_count; i++) COLOUR_RAM[torch_cells[i]] = flame_colours[rnd() & 3];
    }

    // A star twinkles
    if (star_count && !(f & 7)) {
        i = rnd() % star_count;
        COLOUR_RAM[star_cells[i]] = MC + star_colours[rnd() & 3];
    }

    // Lightning: a white flash and a flicker, then thunder a moment later
    back = theme ? theme->back : VCOL_BLACK;
    if (gfx_storm) {
        if (flash) {
            flash--;
            back = (flash == 4 || flash == 1) ? VCOL_WHITE : (flash == 3 ? VCOL_LT_BLUE : back);
        } else if (!--next_strike) {
            flash = 5;
            thunder = 12;
            next_strike = 150 + rnd();
        }
        if (thunder && !--thunder) sound_play(SFX_THUNDER);
    }
    splits[0].back = back;
    splits[0].border = flash == 4 ? VCOL_LT_GREY : VCOL_BLACK;

    // Screen shake: the play area only, side to side
    gfx_shake_x = 0;
    if (shake) {
        shake--;
        if (shake & 1) gfx_shake_x = shake > 6 ? 4 : 2;
    }
    splits[0].d016 = D016_NORMAL | gfx_shake_x;
}
