// The Amiga side of src/room.h: unpack a level, keep its collision map,
// and draw it into the front playfield
#include "gcc8_c_support.h"
#include "room.h"
#include "roomgfx.h"
#include "video.h"
#include "gfx_data.h"

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

unsigned char room_coll[(ROOM_ROWS + 2) * 32];
unsigned char room_current;
unsigned int room_trophy;

static unsigned char tiles[ROOM_ROWS * 32];

#define MAX_EXITS 12
static unsigned short exit_cells[MAX_EXITS];
static unsigned char exit_count;
static unsigned char exit_tile = AT_EXIT;   // its current frame

static int is_solid(unsigned char t) {
    return t == T_BRICK || t == T_BLOCK;
}

static unsigned char cell_tile(unsigned int i) {
    switch (tiles[i]) {
        case T_BRICK:      return (i >= 32 && !is_solid(tiles[i - 32])) ? AT_BRICK_TOP : AT_BRICK;
        case T_BLOCK:      return AT_BLOCK;
        case T_PLATFORM:   return AT_PLAT;
        case T_SPIKE_UP:   return AT_SPIKE_UP;
        case T_SPIKE_DOWN: return AT_SPIKE_DOWN;
        case T_EXIT:       return AT_EXIT;
        case T_TROPHY:     return AT_TROPHY;
    }
    return 0xff;
}

void room_load(unsigned char n) {
    const unsigned char *src = rooms[n].map;
    unsigned char *t = tiles;
    unsigned char v, run, tile;
    unsigned int i;

    room_current = n;
    // Each byte is (tile << 5) | (run - 1)
    while (t < tiles + ROOM_ROWS * 32) {
        v = *src >> 5;
        run = (*src++ & 31) + 1;
        do {
            *t++ = v;
        } while (--run);
    }

    room_trophy = NO_TROPHY;
    exit_count = 0;
    video_clear_world();
    for (i = 0; i < ROOM_ROWS * 32; i++) {
        room_coll[i] = tile_coll[tiles[i]];
        if (tiles[i] == T_TROPHY) room_trophy = i;
        if (tiles[i] == T_EXIT && exit_count < MAX_EXITS) exit_cells[exit_count++] = i;
        tile = cell_tile(i);
        if (tile != 0xff) video_draw_cell(i & 31, i >> 5, tile);
    }
    memset(room_coll + ROOM_ROWS * 32, 0, 2 * 32);
}

void room_set_trophy(unsigned char present) {
    if (room_trophy != NO_TROPHY) room_coll[room_trophy] = present ? TILE_TROPHY : 0;
}

void room_draw_trophy(unsigned char present) {
    if (room_trophy == NO_TROPHY) return;
    video_draw_cell(room_trophy & 31, room_trophy >> 5, present ? AT_TROPHY : AT_EMPTY);
}

void room_animate(unsigned short frame) {
    static const unsigned char exit_frames[4] = { AT_EXIT, AT_EXIT_1, AT_EXIT_2, AT_EXIT_3 };
    unsigned char i;

    if (frame & 3) return;
    exit_tile = exit_frames[(frame >> 2) & 3];
    for (i = 0; i < exit_count; i++) {
        video_draw_cell(exit_cells[i] & 31, exit_cells[i] >> 5, exit_tile);
    }
}

unsigned char room_cell_tile(unsigned int cell) {
    unsigned char t;

    if (tiles[cell] == T_EXIT) return exit_tile;
    if (tiles[cell] == T_TROPHY && !(room_coll[cell] & TILE_TROPHY)) return AT_EMPTY;
    t = cell_tile(cell);
    return t == 0xff ? AT_EMPTY : t;
}

int room_exit_cell(void) {
    return exit_count ? exit_cells[0] : -1;
}
