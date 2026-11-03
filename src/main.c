#include <arch/zx.h>
#include <intrinsic.h>
#include <sound.h>
#include "fixed.h"
#include "input.h"
#include "interrupts.h"
#include "player.h"
#include "enemy.h"
#include "render.h"
#include "room.h"
#include "hud.h"

// Keep the stack in uncontended RAM, clear of the IM2 table at $FE00
#pragma output REGISTER_SP = 0xFDFC

// Set to 1 to show CPU use in the border: red = drawing, blue = game logic,
// black = idle until the next frame. If the bars run past the next interrupt
// the frame overruns, and the following frame's drawing bar is white.
#define DEBUG_TIMING 0

#if DEBUG_TIMING
#define TIMING(c) zx_border(c)
#else
#define TIMING(c)
#endif

#define STATE_PLAY  0
#define STATE_DYING 1

// Death sequence, in frames
#define DEATH_HITSTOP 12        // frozen, flashing, with a falling zap
#define DEATH_PAUSE   25        // empty screen after falling off the bottom

#define COLOUR_DEAD   0x42      // bright red ink

// After respawning, enemies can't hurt you for this long (the player flashes)
#define SAFE_FRAMES   75

// Most logic steps run in one frame to catch up after an overrun. More than
// this and the game would visibly jump ahead after a long stall.
#define MAX_CATCHUP   2

static unsigned char state;
static unsigned char death_timer;
static unsigned char player_visible;
static unsigned char player_colour;
static unsigned char safe_timer;

// The last place the player stood safely. Only updated while standing on
// the ground, so it never ends up mid-air or over a pit - and if you die
// in a new room before landing anywhere, you go back to the previous one.
static unsigned char respawn_room;
static unsigned char respawn_x;
static unsigned char respawn_y;
static unsigned char respawn_facing_left;

// Frames where the work didn't finish in time (watch it in a debugger)
unsigned int frame_overruns;

static void enter_room(unsigned char n) {
    room_load(n);
    hud_room(rooms[n].name);
    enemies_load(rooms[n].enemies, rooms[n].enemy_count);
    sprites_reset();
    // Drawing the room takes a few frames: restart the frame timing rather
    // than count that as an overrun to catch up on
    wait_frame();
}

static void respawn(void) {
    if (respawn_room != room_current) {
        enter_room(respawn_room);
    } else {
        // Enemies go back to their start too
        enemies_load(rooms[room_current].enemies, rooms[room_current].enemy_count);
    }
    player.facing_left = respawn_facing_left;
    player_spawn(respawn_x, respawn_y);
    player_visible = 1;
    player_colour = 0;
    safe_timer = SAFE_FRAMES;
    state = STATE_PLAY;
}

// bit_beep returns with interrupts disabled, which would make the next HALT
// wait forever
static void beep(unsigned int cycles, unsigned int period) {
    bit_beep(cycles, period);
    intrinsic_ei();
}

static void start_death(void) {
    state = STATE_DYING;
    death_timer = 0;
    player_die();
}

// Everything else is frozen while this plays out
static void update_death(void) {
    ++death_timer;
    if (death_timer < DEATH_HITSTOP) {
        player_colour = (death_timer & 2) ? COLOUR_DEAD : 0;
        beep(4, 300 + death_timer * 60);
    } else if (death_timer == DEATH_HITSTOP) {
        player_colour = COLOUR_DEAD;
        player_death_pop();
    } else if (player_visible) {
        if (player_death_fall()) {
            player_visible = 0;
            death_timer = DEATH_HITSTOP;
        }
    } else if (death_timer == DEATH_HITSTOP + DEATH_PAUSE) {
        respawn();
    }
}

// Rooms are ROOM_BOTTOM_Y apart vertically, so moving between them keeps
// the sub-pixel position and the velocity: a jump carries on into the room
// above, a fall into the room below.
static void handle_event(unsigned char ev) {
    const room_t *room = &rooms[room_current];

    switch (ev) {
        case EV_EXIT_LEFT:
            if (room->left != NO_ROOM) {
                enter_room(room->left);
                player.x = FIX_FROM_INT(SPR_MAX_X);
            } else {
                player.vx = 0;
            }
            break;

        case EV_EXIT_RIGHT:
            if (room->right != NO_ROOM) {
                enter_room(room->right);
                player.x = 0;
            } else {
                player.vx = 0;
            }
            break;

        case EV_EXIT_UP:
            if (room->up != NO_ROOM) {
                enter_room(room->up);
                player.y += FIX_FROM_INT(ROOM_BOTTOM_Y);
            } else {
                // The top of the world is an invisible ceiling
                player.y = 0;
                player.vy = 0;
                player.jumping = 0;
            }
            break;

        case EV_EXIT_DOWN:
            if (room->down != NO_ROOM) {
                enter_room(room->down);
                player.y -= FIX_FROM_INT(ROOM_BOTTOM_Y);
            } else {
                // A bottomless pit. Keep the body out of the HUD.
                player.y = FIX_FROM_INT(ROOM_BOTTOM_Y);
                start_death();
            }
            break;

        case EV_DIED:
            start_death();
            break;
    }
}

// Copy game state into the sprite list for the next frame's render
static void sync_sprites(void) {
    sprite_t *s = sprites;
    enemy_t *e = enemies;
    unsigned char i;

    s->x = FIX_WHOLE(player.x);
    s->y = FIX_WHOLE(player.y);
    s->frame = player.frame;
    s->colour = player_colour;
    s->visible = player_visible && !(safe_timer & 4);

    for (i = enemy_count; i; --i, ++e) {
        ++s;
        s->x = FIX_WHOLE(e->x);
        s->y = FIX_WHOLE(e->y);
        s->frame = e->frame;
        s->colour = e->colour;
        s->visible = 1;
    }
    sprite_count = 1 + enemy_count;
}

// One 50Hz step of game logic
static void game_tick(unsigned char held, unsigned char pressed) {
    unsigned char ev;

    if (state != STATE_PLAY) {
        update_death();
        return;
    }

    enemies_update();
    ev = player_update(held, pressed);
    if (safe_timer) {
        safe_timer--;
    } else if (!ev && enemies_hit(FIX_WHOLE(player.x) + PLAYER_HURT_LEFT,
                                  FIX_WHOLE(player.y) + PLAYER_HURT_TOP,
                                  FIX_WHOLE(player.x) + PLAYER_HURT_RIGHT,
                                  FIX_WHOLE(player.y) + PLAYER_HURT_BOTTOM)) {
        ev = EV_DIED;
    }

    if (ev) {
        handle_event(ev);
    } else if (player.on_ground) {
        respawn_room = room_current;
        respawn_x = FIX_WHOLE(player.x);
        respawn_y = FIX_WHOLE(player.y);
        respawn_facing_left = player.facing_left;
    }
}

int main(void) {
    unsigned char held;
    unsigned char prev = 0;
    unsigned char pressed;
    unsigned char ticks;

    interrupts_init();
    zx_border(INK_BLACK);
    render_init();
    input_init();

    respawn_room = room_start;
    respawn_x = room_start_x;
    respawn_y = room_start_y;
    enter_room(room_start);
    respawn();
    safe_timer = 0;
    sync_sprites();

    for (;;) {
        // Sync to the frame interrupt, which fires at the start of the top
        // border. If last frame's work overran, this returns at once instead.
        ticks = wait_frame();
        if (frame_late) frame_overruns++;

        // Draw first, using last frame's state: the beam is still in the top
        // border, so sprites are updated before it reaches them.
        TIMING(frame_late ? INK_WHITE : INK_RED);
        sprites_render();

        // Normally one logic step per frame. If a whole frame was lost, run
        // the missed steps too, so the game keeps real-time speed and only
        // the display skips.
        TIMING(INK_BLUE);
        held = input_read();
        pressed = held & ~prev;
        prev = held;
        if (ticks > MAX_CATCHUP) ticks = MAX_CATCHUP;
        do {
            game_tick(held, pressed);
            pressed = 0;
        } while (--ticks);
        sync_sprites();

        TIMING(INK_BLACK);
    }
}
