#include "player.h"
#include "input.h"
#include "room.h"
#include "render.h"

// All tuning values are 8.8 fixed point per 50Hz frame ($0100 = 1 pixel).
#define RUN_MAX       0x0180    // 1.5 px/frame top speed
#define GROUND_ACCEL  0x0028    // ~10 frames to top speed
#define GROUND_DECEL  0x0030    // friction with no input
#define TURN_ACCEL    0x0060    // skid when reversing on the ground
#define AIR_ACCEL     0x0020    // reduced, but still real, air control
#define AIR_DECEL     0x0010    // keep momentum through jumps

#define JUMP_VEL      (-0x0400) // 4 px/frame launch
#define GRAV_RISE     0x0038    // rising with jump held: ~36 px full jump
#define GRAV_CUT      0x00A0    // rising after jump released: short hop
#define GRAV_FALL     0x0050    // falling is snappier than rising
#define APEX_BAND     0x0080    // |vy| below this near the top of a held jump...
                                // ...halves gravity for a little hang time
#define MAX_FALL      0x0400    // 4 px/frame: under a tile, so no tunnelling

#define COYOTE_FRAMES 4         // can still jump this long after leaving a ledge
#define BUFFER_FRAMES 6         // a jump pressed this early before landing still fires

#define WALL_JUMP_VX       RUN_MAX     // kick away from the wall at full speed
#define WALL_JUMP_VY       (-0x0380)   // a little lower than a ground jump
#define WALL_SLIDE_MAX     0x00C0      // fall speed cap while pushing into a wall
#define WALL_COYOTE_FRAMES 4           // can still wall jump this long after letting go
#define WALL_LOCK_FRAMES   10          // no steering for this long after a wall jump, so
                                       // holding towards the wall can't cancel the kick

#define DEATH_POP_VEL (-0x0300)
#define DEATH_GRAV    0x0030

player_t player;

void player_spawn(unsigned char x, unsigned char y) {
    player.x = FIX_FROM_INT(x);
    player.y = FIX_FROM_INT(y);
    player.vx = 0;
    player.vy = 0;
    player.on_ground = 0;
    player.jumping = 0;
    player.coyote = 0;
    player.jump_buffer = 0;
    player.wall = 0;
    player.wall_coyote = 0;
    player.wall_lock = 0;
    player.sliding = 0;
    player.anim = 0;
    player.frame = SPR_JUMP + (player.facing_left ? SPR_LEFT : 0);
}

// Any solid cell in the pixel column x, over the rows the sprite at 'top' covers
static unsigned char solid_column(unsigned char x, unsigned char top) {
    unsigned char *p = &room_coll[((unsigned int)(top & 0xF8) << 2) + (x >> 3)];

    if ((p[0] | p[32]) & TILE_SOLID) return 1;
    // A sprite not aligned to a cell row overlaps a third row
    if ((top & 7) && (p[64] & TILE_SOLID)) return 1;
    return 0;
}

// OR of the flags of every cell in pixel row y under the collision box
static unsigned char row_flags(unsigned char y, unsigned char left) {
    unsigned char *p = &room_coll[(unsigned int)(y & 0xF8) << 2];
    unsigned char c = left >> 3;
    unsigned char last = (left + (PLAYER_HB_W - 1)) >> 3;
    unsigned char f = 0;

    do {
        f |= p[c];
    } while (++c <= last);
    return f;
}

// Spike art fills only part of its cell, so the bottom of the hurt box is
// pulled in by 3 pixels: you die touching the points, not the empty space
// above them. The box is then 8x10, so it touches at most 2x2 cells.
static unsigned char touching_hazard(void) {
    unsigned char left = (FIX_WHOLE(player.x) + PLAYER_HURT_LEFT) >> 3;
    unsigned char right = (FIX_WHOLE(player.x) + PLAYER_HURT_RIGHT) >> 3;
    unsigned char top = FIX_WHOLE(player.y) + PLAYER_HURT_TOP;
    unsigned char bottom = FIX_WHOLE(player.y) + PLAYER_HURT_BOTTOM - 3;
    unsigned char *p = &room_coll[(unsigned int)(top & 0xF8) << 2];
    unsigned char *q = &room_coll[(unsigned int)(bottom & 0xF8) << 2];

    return (p[left] | p[right] | q[left] | q[right]) & TILE_HAZARD;
}

static void update_horizontal(unsigned char held) {
    sfix8 vx = player.vx;
    sfix8 accel, decel, turn;

    // Keep the wall jump's kick-off momentum untouched for a moment
    if (player.wall_lock) {
        player.wall_lock--;
        return;
    }

    if (player.on_ground) {
        accel = GROUND_ACCEL;
        decel = GROUND_DECEL;
        turn = TURN_ACCEL;
    } else {
        accel = AIR_ACCEL;
        decel = AIR_DECEL;
        turn = AIR_ACCEL;
    }

    if (held & IN_RIGHT) {
        player.facing_left = 0;
        vx += (vx < 0) ? turn : accel;
        if (vx > RUN_MAX) vx = RUN_MAX;
    } else if (held & IN_LEFT) {
        player.facing_left = 1;
        vx -= (vx > 0) ? turn : accel;
        if (vx < -RUN_MAX) vx = -RUN_MAX;
    } else if (vx > 0) {
        vx -= decel;
        if (vx < 0) vx = 0;
    } else if (vx < 0) {
        vx += decel;
        if (vx > 0) vx = 0;
    }
    player.vx = vx;
}

static void update_vertical(unsigned char held, unsigned char pressed) {
    sfix8 vy;
    sfix8 g;
    unsigned char holding = player.jumping && (held & IN_JUMP);

    // Letting go ends the variable-height phase for good, so re-pressing
    // mid-air can't bring the float back
    if (!(held & IN_JUMP)) player.jumping = 0;

    // Jump buffering: remember a press for a few frames so one made just
    // before landing (or reaching a wall) still counts
    if (pressed & IN_JUMP) {
        player.jump_buffer = BUFFER_FRAMES;
    } else if (player.jump_buffer) {
        player.jump_buffer--;
    }

    if (player.jump_buffer) {
        // Coyote time: 'coyote' stays non-zero briefly after walking off a ledge
        if (player.coyote) {
            player.vy = JUMP_VEL;
        } else if (player.wall_coyote) {
            // Wall jump: kick away from the last wall touched
            player.vy = WALL_JUMP_VY;
            if (player.wall_side == WALL_LEFT) {
                player.vx = WALL_JUMP_VX;
                player.facing_left = 0;
            } else {
                player.vx = -WALL_JUMP_VX;
                player.facing_left = 1;
            }
            player.wall_lock = WALL_LOCK_FRAMES;
        }
        if (player.coyote || player.wall_coyote) {
            player.jumping = 1;
            player.jump_buffer = 0;
            player.coyote = 0;
            player.wall_coyote = 0;
            player.on_ground = 0;
            holding = (held & IN_JUMP) != 0;
        }
    }

    // Pushing into a wall while airborne slows the fall to a slide
    player.sliding = player.wall &&
                     (held & ((player.wall == WALL_LEFT) ? IN_LEFT : IN_RIGHT)) != 0;

    // Variable gravity: holding jump gives a high floaty arc, letting go cuts
    // it short, and falling is heavier than rising
    vy = player.vy;
    if (vy < 0) {
        g = holding ? GRAV_RISE : GRAV_CUT;
    } else {
        g = GRAV_FALL;
    }
    if (holding && vy > -APEX_BAND && vy < APEX_BAND) g >>= 1;

    vy += g;
    if (vy > MAX_FALL) vy = MAX_FALL;
    if (player.sliding && vy > WALL_SLIDE_MAX) vy = WALL_SLIDE_MAX;
    if (vy >= APEX_BAND) player.jumping = 0;
    player.vy = vy;
}

static unsigned char move_horizontal(void) {
    ufix8 nx = player.x + player.vx;
    unsigned char px, py, edge;

    // Leaving the screen: the unsigned position wraps when going left
    if (player.vx < 0 && nx > player.x) {
        player.x = 0;
        return EV_EXIT_LEFT;
    }
    if (FIX_WHOLE(nx) > SPR_MAX_X) {
        player.x = FIX_FROM_INT(SPR_MAX_X);
        return EV_EXIT_RIGHT;
    }

    px = FIX_WHOLE(nx);
    py = FIX_WHOLE(player.y);

    if (player.vx > 0) {
        edge = px + (PLAYER_HB_X + PLAYER_HB_W - 1);
        if (solid_column(edge, py)) {
            nx = FIX_FROM_INT((edge & 0xF8) - (PLAYER_HB_X + PLAYER_HB_W));
            player.vx = 0;
        }
    } else if (player.vx < 0) {
        edge = px + PLAYER_HB_X;
        if (solid_column(edge, py)) {
            nx = FIX_FROM_INT((edge & 0xF8) + 8 - PLAYER_HB_X);
            player.vx = 0;
        }
    }
    player.x = nx;
    return EV_NONE;
}

static unsigned char move_vertical(unsigned char held) {
    ufix8 ny = player.y + player.vy;
    unsigned char old_py = FIX_WHOLE(player.y);
    unsigned char py, foot, flags;
    unsigned char left = FIX_WHOLE(player.x) + PLAYER_HB_X;

    // Off the top (the unsigned position wraps) or out of the bottom: the
    // caller moves us to the next room, or stops us at the edge
    if (player.vy < 0 && ny > player.y) {
        player.y = ny;
        return EV_EXIT_UP;
    }
    py = FIX_WHOLE(ny);
    if (py > ROOM_BOTTOM_Y) {
        player.y = ny;
        return EV_EXIT_DOWN;
    }

    player.on_ground = 0;
    if (player.vy >= 0) {
        // Check the pixel row just below the feet. Whether the feet are
        // exactly on a cell boundary or have sunk into a cell, that row is
        // the one that decides whether we're standing.
        foot = py + PLAYER_HEIGHT;
        flags = row_flags(foot, left);
        if ((flags & TILE_SOLID) ||
            ((flags & TILE_PLATFORM) && !(held & IN_DOWN) &&
             old_py + PLAYER_HEIGHT <= (foot & 0xF8))) {
            ny = FIX_FROM_INT((foot & 0xF8) - PLAYER_HEIGHT);
            player.vy = 0;
            player.on_ground = 1;
            player.jumping = 0;
        }
    } else {
        if (row_flags(py, left) & TILE_SOLID) {
            ny = FIX_FROM_INT((py & 0xF8) + 8);
            player.vy = 0;
            player.jumping = 0;
        }
    }
    player.y = ny;
    return EV_NONE;
}

// Probe the pixel columns just outside the collision box. Done after moving,
// like the ground check, so next frame's jump sees where we ended up.
static void detect_wall(void) {
    unsigned char left = FIX_WHOLE(player.x) + PLAYER_HB_X;
    unsigned char top = FIX_WHOLE(player.y);

    player.wall = 0;
    if (!player.on_ground) {
        if (solid_column(left - 1, top)) {
            player.wall = WALL_LEFT;
        } else if (solid_column(left + PLAYER_HB_W, top)) {
            player.wall = WALL_RIGHT;
        }
    }

    if (player.wall) {
        player.wall_side = player.wall;
        player.wall_coyote = WALL_COYOTE_FRAMES;
    } else if (player.wall_coyote) {
        player.wall_coyote--;
    }
}

static void update_animation(void) {
    unsigned char f;

    if (player.sliding && player.vy > 0) {
        // The wall frame has the wall on the right; mirrored for a left wall
        f = SPR_WALL + ((player.wall == WALL_LEFT) ? SPR_LEFT : 0);
        player.frame = f;
        return;
    }

    if (!player.on_ground) {
        f = SPR_JUMP;
    } else if (player.vx == 0) {
        f = SPR_IDLE;
        player.anim = 0;
    } else {
        // Step every 8 pixels travelled, so the legs match the ground speed
        player.anim += (player.vx < 0) ? -player.vx : player.vx;
        f = (player.anim & 0x0800) ? SPR_RUN_B : SPR_RUN_A;
    }
    if (player.facing_left) f += SPR_LEFT;
    player.frame = f;
}

unsigned char player_update(unsigned char held, unsigned char pressed) {
    unsigned char ev;

    update_horizontal(held);
    update_vertical(held, pressed);

    ev = move_horizontal();
    if (ev) return ev;
    ev = move_vertical(held);
    if (ev) return ev;

    if (player.on_ground) {
        player.coyote = COYOTE_FRAMES;
    } else if (player.coyote) {
        player.coyote--;
    }
    detect_wall();

    update_animation();

    if (touching_hazard()) return EV_DIED;
    return EV_NONE;
}

void player_die(void) {
    player.vx = 0;
    player.vy = 0;
    player.frame = SPR_DEAD;
}

void player_death_pop(void) {
    player.vy = DEATH_POP_VEL;
}

// No collision: the body pops up and drops straight through the floor
unsigned char player_death_fall(void) {
    ufix8 ny;

    player.vy += DEATH_GRAV;
    if (player.vy > MAX_FALL) player.vy = MAX_FALL;
    ny = player.y + player.vy;
    if (player.vy < 0 && ny > player.y) ny = 0;
    player.y = ny;
    return FIX_WHOLE(ny) > ROOM_BOTTOM_Y;
}
