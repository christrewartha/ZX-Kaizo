#include <string.h>
#include "room.h"
#include "render.h"

// Tile ids, as produced by tools/build_data.ps1 from the map characters
#define T_EMPTY      0          // ' '
#define T_BRICK      1          // '#'
#define T_BLOCK      2          // 'X'
#define T_PLATFORM   3          // '='
#define T_SPIKE_UP   4          // '^'
#define T_SPIKE_DOWN 5          // 'v'
#define T_COUNT      6

// Tile graphics (8 bytes, top line first)
static const unsigned char tile_gfx[T_COUNT][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xFE, 0xFE, 0xFE, 0x00, 0xEF, 0xEF, 0xEF, 0x00 },
    { 0xFF, 0x81, 0xBD, 0xA5, 0xA5, 0xBD, 0x81, 0xFF },
    { 0xFF, 0xAA, 0x55, 0xFF, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x22, 0x22, 0x77, 0x77, 0xFF },
    { 0xFF, 0x77, 0x77, 0x22, 0x22, 0x00, 0x00, 0x00 },
};

// Black paper everywhere so the player (no attribute of its own) takes the
// cell's ink: bright white in open space, never clashing with solid tiles.
static const unsigned char tile_attr[T_COUNT] = { 0x47, 0x42, 0x45, 0x46, 0x47, 0x47 };
static const unsigned char tile_coll[T_COUNT] = {
    0, TILE_SOLID, TILE_SOLID, TILE_PLATFORM, TILE_HAZARD, TILE_HAZARD
};


#define ROOM_CELLS (ROOM_ROWS * 32)

unsigned char room_coll[(ROOM_ROWS + 2) * 32];
unsigned char room_current;

void room_load(unsigned char n) {
    const unsigned char *src = rooms[n].map;
    unsigned char *coll = room_coll;
    unsigned char *attr = SCREEN_ATTR;
    unsigned char *scr;
    const unsigned char *gfx;
    unsigned char row, col, line, t, run;
    unsigned int i;

    room_current = n;

    // Unpack the tile ids into room_coll; the rows below the room stay empty
    while (coll < room_coll + ROOM_CELLS) {
        t = *src >> 5;
        run = (*src++ & 31) + 1;
        do {
            *coll++ = t;
        } while (--run);
    }
    memset(room_coll + ROOM_CELLS, T_EMPTY, sizeof(room_coll) - ROOM_CELLS);

    // Hide the screen while drawing, then reveal it in one go via the attributes
    memset(SCREEN_ATTR, 0, 768);
    memset(SCREEN_BITMAP, 0, 6144);

    coll = room_coll;
    for (row = 0; row < ROOM_ROWS; row++) {
        for (col = 0; col < 32; col++) {
            t = *coll++;
            if (t != T_EMPTY) {
                scr = (unsigned char *)(screen_row[row << 3] + col);
                gfx = tile_gfx[t];
                for (line = 0; line < 8; line++) {
                    *scr = *gfx++;
                    scr += 256;
                }
            }
        }
    }

    // Colour the screen and swap the tile ids for collision flags
    coll = room_coll;
    for (i = 0; i < ROOM_CELLS; i++) {
        t = *coll;
        *attr++ = tile_attr[t];
        *coll++ = tile_coll[t];
    }
}
