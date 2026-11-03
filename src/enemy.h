#ifndef ENEMY_H
#define ENEMY_H

#include "fixed.h"

// Guardians patrol back and forth between two limits on one axis
#define ENEMY_WALKER 0          // horizontal: min/max are x limits
#define ENEMY_FLYER  1          // vertical: min/max are y limits

#define MAX_ENEMIES 4

// Placement in a room. min must be above 0: the patrol relies on not wrapping.
typedef struct {
    unsigned char type;
    unsigned char x, y;         // start position (sprite top-left)
    unsigned char min, max;     // patrol limits along the axis of travel
    sfix8 speed;                // 8.8 px/frame; the sign is the starting direction
    unsigned char colour;       // ink (+ BRIGHT) painted on the cells it covers
} enemy_def;

typedef struct {
    unsigned char type;
    ufix8 x, y;
    sfix8 vel;
    unsigned char min, max;
    unsigned char colour;
    unsigned char frame;
    unsigned int  anim;
} enemy_t;

extern enemy_t enemies[MAX_ENEMIES];
extern unsigned char enemy_count;

// Reset the enemies to a room's starting placement
void enemies_load(const enemy_def *def, unsigned char count);

void enemies_update(void);

// Non-zero if any enemy overlaps the given box (inclusive pixel bounds)
unsigned char enemies_hit(unsigned char left, unsigned char top, unsigned char right, unsigned char bottom);

#endif
