#ifndef ROOM_H
#define ROOM_H

#include "enemy.h"

// Collision flags stored per 8x8 cell
#define TILE_SOLID    0x01      // blocks from every side
#define TILE_PLATFORM 0x02      // one-way: only lands from above
#define TILE_HAZARD   0x04      // kills on contact

#define NO_ROOM 0xFF

// The play area is the top 22 character rows; the HUD has the last 2
#define ROOM_ROWS     22
#define ROOM_BOTTOM_Y (ROOM_ROWS * 8 - 16)  // lowest sprite y inside a room

// Rooms are authored on a grid in data/world.txt and packed by
// tools/build_data.ps1 into build/gen/rooms_data.c.
typedef struct {
    const unsigned char *map;   // 32 x ROOM_ROWS cells run-length encoded:
                                // each byte is (tile id << 5) | (run length - 1)
    unsigned char left;         // neighbouring room indices, or NO_ROOM
    unsigned char right;
    unsigned char up;
    unsigned char down;
    const char *name;
    const enemy_def *enemies;
    unsigned char enemy_count;
} room_t;

extern const room_t rooms[];

// Where the game begins: room index and player position
extern const unsigned char room_start;
extern const unsigned char room_start_x;
extern const unsigned char room_start_y;

// Collision map for the current room, indexed (row << 5) | column. Two
// extra empty rows below the room, so probes just past the bottom edge
// (where the player falls out) read as open space.
extern unsigned char room_coll[(ROOM_ROWS + 2) * 32];

extern unsigned char room_current;

// Decode and draw a room. The screen is redrawn, so the caller must
// sprites_reset() before the next sprites_render().
void room_load(unsigned char n);

#endif
