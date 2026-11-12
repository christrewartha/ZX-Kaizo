// The Amiga side of src/room.h: unpack a level, keep its collision map,
// and draw it into the playfield in Sunny Land's tiles (by ansimuz)
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

// The exit is drawn as a door, 2 cells by 3, standing on the floor of the
// exit's area
static unsigned char exit_left, exit_bottom;
static unsigned char door_open;
static unsigned char gem_frame;

// The 3x3 sets of edges, top-left to bottom-right, outdoors and in
static const unsigned char earth[9] = {
    AT_OUT_TL, AT_OUT_T, AT_OUT_TR, AT_OUT_L, AT_OUT_C, AT_OUT_R, AT_OUT_BL, AT_OUT_B, AT_OUT_BR
};
static const unsigned char stone[9] = {
    AT_IN_TL, AT_IN_T, AT_IN_TR, AT_IN_L, AT_IN_C, AT_IN_R, AT_IN_BL, AT_IN_B, AT_IN_BR
};
static const unsigned char gems[5] = { AT_GEM_1, AT_GEM_2, AT_GEM_3, AT_GEM_4, AT_GEM_5 };

// Off the edges counts as solid: the screen's edges are walls
static int tile_at(int r, int c) {
    if (r < 0 || c < 0 || c > 31) return T_BRICK;
    if (r >= ROOM_ROWS) return T_EMPTY;
    return tiles[r * 32 + c];
}

static int is_solid(int r, int c) {
    return tile_at(r, c) == T_BRICK;
}

// Ground: which piece of the 3x3 set, from which sides are open
static unsigned char ground(int r, int c) {
    int row = !is_solid(r - 1, c) ? 0 : !is_solid(r + 1, c) ? 2 : 1;
    int col = !is_solid(r, c - 1) ? 0 : !is_solid(r, c + 1) ? 2 : 1;

    return (video_outdoors() ? earth : stone)[row * 3 + col];
}


static unsigned char cell_tile(unsigned int i) {
    int r = i >> 5, c = i & 31;

    switch (tiles[i]) {
        case T_BRICK:
            return ground(r, c);
        case T_BLOCK:
            return AT_BLOCK;
        case T_PLATFORM: {
            int l = tile_at(r, c - 1) == T_PLATFORM, rt = tile_at(r, c + 1) == T_PLATFORM;
            return (!l && rt) ? AT_LOG_L : (l && !rt) ? AT_LOG_R : AT_LOG_M;
        }
        case T_SPIKE_UP:
            return AT_SPIKE_UP;
        case T_SPIKE_DOWN:
            return AT_SPIKE_DOWN;
        case T_EXIT: {
            int dx = c - exit_left, up = exit_bottom - r;
            if (dx < 0 || dx > 1 || up < 0 || up > 2) return AT_EMPTY;
            return (door_open ? AT_DOOR_OPEN_0 : AT_DOOR_0) + (2 - up) * 2 + dx;
        }
        case T_TROPHY:
            return (room_coll[i] & TILE_TROPHY) ? gems[gem_frame] : AT_EMPTY;
    }
    return AT_EMPTY;
}

static void draw_all(void) {
    video_clear_world();
    for (unsigned int i = 0; i < ROOM_ROWS * 32; i++) {
        unsigned char t = cell_tile(i);
        if (t != AT_EMPTY) video_draw_cell(i & 31, i >> 5, t);
    }
}

void room_load(unsigned char n) {
    const unsigned char *src = rooms[n].map;
    unsigned char *t = tiles;
    unsigned char v, run;
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
    exit_left = 31;
    exit_bottom = 0;
    for (i = 0; i < ROOM_ROWS * 32; i++) {
        room_coll[i] = tile_coll[tiles[i]];
        if (tiles[i] == T_TROPHY) room_trophy = i;
        if (tiles[i] == T_EXIT) {
            if ((i & 31) < exit_left) exit_left = i & 31;
            if ((i >> 5) > exit_bottom) exit_bottom = i >> 5;
        }
    }
    memset(room_coll + ROOM_ROWS * 32, 0, 2 * 32);
    door_open = 0;
    gem_frame = 0;
    draw_all();
}

void room_title(void) {
    // The title's world: open sky over two rows of ground
    memset(tiles, T_EMPTY, sizeof(tiles));
    memset(tiles + (ROOM_ROWS - 2) * 32, T_BRICK, 2 * 32);
    memset(room_coll, 0, sizeof(room_coll));
    room_trophy = NO_TROPHY;
    exit_left = 31;
    exit_bottom = 0;
    draw_all();
}

void room_set_trophy(unsigned char present) {
    if (room_trophy != NO_TROPHY) room_coll[room_trophy] = present ? TILE_TROPHY : 0;
}

void room_draw_trophy(unsigned char present) {
    (void)present;
    if (room_trophy == NO_TROPHY) return;
    video_draw_cell(room_trophy & 31, room_trophy >> 5, cell_tile(room_trophy));
}

static void draw_door(void) {
    for (int up = 0; up < 3; up++) {
        for (int dx = 0; dx < 2; dx++) {
            int r = exit_bottom - up, c = exit_left + dx;
            if (r >= 0 && tiles[r * 32 + c] == T_EXIT) video_draw_cell(c, r, cell_tile(r * 32 + c));
        }
    }
}

void room_open_door(void) {
    door_open = 1;
    draw_door();
}

// The gem turns
void room_animate(unsigned short frame) {
    if (frame & 7) return;
    gem_frame = (gem_frame + 1) % 5;
    if (room_trophy != NO_TROPHY && (room_coll[room_trophy] & TILE_TROPHY)) {
        video_draw_cell(room_trophy & 31, room_trophy >> 5, gems[gem_frame]);
    }
}


int room_exit_cell(void) {
    return exit_bottom * 32 + exit_left;
}
