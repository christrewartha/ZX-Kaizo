#ifndef PLAYER_H
#define PLAYER_H

#include "fixed.h"

// Collision box inside the 16x16 sprite: narrower than the art so the
// player can slip past corners and hang a little off ledges.
#define PLAYER_HB_X 3
#define PLAYER_HB_W 10
#define PLAYER_HEIGHT 16

// Smaller box for getting hurt, so grazes don't kill
#define PLAYER_HURT_LEFT   4
#define PLAYER_HURT_RIGHT  11
#define PLAYER_HURT_TOP    3
#define PLAYER_HURT_BOTTOM 15

// Which side a wall is touching
#define WALL_LEFT  1
#define WALL_RIGHT 2

// player_update() results for main.c to handle
#define EV_NONE       0
#define EV_FELL       1         // out of the bottom: y is below ROOM_BOTTOM_Y
#define EV_DIED       2
#define EV_DONE       3         // touched the exit

typedef struct {
    ufix8 x, y;                 // top-left of the sprite
    sfix8 vx, vy;
    unsigned char on_ground;
    unsigned char jumping;      // rising from a jump: variable height still applies
    unsigned char coyote;       // frames left in which a jump is still allowed
    unsigned char jump_buffer;  // frames left in which a jump press is remembered
    unsigned char wall;         // WALL_* touching right now (airborne only), or 0
    unsigned char wall_side;    // side of the last wall touched
    unsigned char wall_coyote;  // frames left in which a wall jump is still allowed
    unsigned char wall_lock;    // frames of kick-off after a wall jump with no steering
    unsigned char sliding;      // pushing into a wall while airborne
    unsigned char facing_left;
    unsigned int  anim;         // distance travelled, drives the run cycle
    unsigned char frame;        // SPR_* frame to draw
} player_t;

extern player_t player;

void player_spawn(unsigned char x, unsigned char y);

// One 50Hz tick. 'held' is this frame's input, 'pressed' the newly pressed bits.
unsigned char player_update(unsigned char held, unsigned char pressed);

// Death: freeze in the dead pose, then pop up and fall off the screen
void player_die(void);
void player_death_pop(void);
// Returns non-zero once the player has fallen off the bottom
unsigned char player_death_fall(void);

#endif
