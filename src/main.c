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
#include "title.h"
#include "music.h"

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
#define STATE_CLEAR 2           // reached the exit
#define STATE_WON   3           // reached the last level's exit

// Death sequence, in frames
#define DEATH_HITSTOP 12        // frozen, flashing, with a falling zap
#define DEATH_PAUSE   25        // empty screen after falling off the bottom

#define COLOUR_DEAD   0x42      // bright red ink

// Level clear fanfare: a rising note every CLEAR_NOTE frames
#define CLEAR_NOTES   8
#define CLEAR_NOTE    4

// Most logic steps run in one frame to catch up after an overrun. More than
// this and the game would visibly jump ahead after a long stall.
#define MAX_CATCHUP   2

static unsigned char state;
static unsigned char state_timer;
static unsigned char player_visible;
static unsigned char player_colour;
static unsigned int deaths;

// The trophies: won so far (picked up, then carried out of the exit), and
// whether this level's is being carried. Dying drops it back in its place.
static unsigned char trophies;
static unsigned char trophy_held;
static unsigned char trophy_drawn;  // whether the screen shows it in place

// Frames where the work didn't finish in time (watch it in a debugger)
unsigned int frame_overruns;

// Put the player at the start of the level, and the enemies at theirs
static void respawn(void) {
    const room_t *room = &rooms[room_current];

    if (trophy_held) {
        trophy_held = 0;
        room_set_trophy(1);
        hud_trophies(trophies, room_count, 0);
    }
    enemies_load(room->enemies, room->enemy_count);
    player.facing_left = 0;
    player_spawn(room->start_x, room->start_y);
    player_visible = 1;
    player_colour = 0;
    state = STATE_PLAY;
}

static void start_level(unsigned char n) {
    room_load(n);
    hud_room(rooms[n].name, n + 1, room_count);
    hud_deaths(deaths);
    hud_trophies(trophies, room_count, 0);
    trophy_held = 0;
    trophy_drawn = 1;
    sprites_reset();
    respawn();
    // Drawing the room takes a few frames: restart the frame timing rather
    // than count that as an overrun to catch up on
    wait_frame();
}

// bit_beep returns with interrupts disabled, which would make the next HALT
// wait forever
static void beep(unsigned int cycles, unsigned int period) {
    bit_beep(cycles, period);
    intrinsic_ei();
}

static void start_death(void) {
    state = STATE_DYING;
    state_timer = 0;
    player_die();
}

// Everything else is frozen while this plays out
static void update_death(void) {
    ++state_timer;
    if (state_timer < DEATH_HITSTOP) {
        player_colour = (state_timer & 2) ? COLOUR_DEAD : 0;
        beep(4, 300 + state_timer * 60);
    } else if (state_timer == DEATH_HITSTOP) {
        player_colour = COLOUR_DEAD;
        player_death_pop();
    } else if (player_visible) {
        if (player_death_fall()) {
            player_visible = 0;
            state_timer = DEATH_HITSTOP;
        }
    } else if (state_timer == DEATH_HITSTOP + DEATH_PAUSE) {
        hud_deaths(++deaths);
        respawn();
    }
}

// The player stands still at the exit while the fanfare plays
static void update_clear(void) {
    ++state_timer;
    if (!(state_timer % CLEAR_NOTE)) {
        beep(12, 900 - state_timer * 20);
    }
    if (state_timer == CLEAR_NOTES * CLEAR_NOTE) {
        if (room_current + 1 == room_count) {
            state = STATE_WON;
        } else {
            start_level(room_current + 1);
        }
    }
}

static void handle_event(unsigned char ev) {
    switch (ev) {
        case EV_FELL:
            // Keep the body out of the HUD
            player.y = FIX_FROM_INT(ROOM_BOTTOM_Y);
            start_death();
            break;

        case EV_DIED:
            start_death();
            break;

        case EV_DONE:
            if (trophy_held) hud_trophies(++trophies, room_count, 1);
            state = STATE_CLEAR;
            state_timer = 0;
            player.frame = SPR_IDLE + (player.facing_left ? SPR_LEFT : 0);
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
    s->visible = player_visible;

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

// Whether the player's collision box is over the trophy
static unsigned char touching_trophy(void) {
    unsigned char left = FIX_WHOLE(player.x) + PLAYER_HB_X;
    unsigned char top = FIX_WHOLE(player.y);
    unsigned char c0 = left >> 3, c1 = (unsigned char)(left + PLAYER_HB_W - 1) >> 3;
    unsigned char r0 = top >> 3, r1 = (unsigned char)(top + PLAYER_HEIGHT - 1) >> 3;
    unsigned char r, c;

    for (r = r0; r <= r1; r++) {
        for (c = c0; c <= c1; c++) {
            if (room_coll[((unsigned int)r << 5) | c] & TILE_TROPHY) return 1;
        }
    }
    return 0;
}

// The trophy is drawn or cleared to match whether it's being carried, but
// only once no sprite is over it: erasing a sprite puts back the screen it
// covered, which would undo the change. Call straight after
// sprites_render(), while 'sprites' says what's on the screen.
static void update_trophy_cell(void) {
    unsigned char want = !trophy_held;
    unsigned char col, row, i;
    sprite_t *s;

    if (trophy_drawn == want || room_trophy == NO_TROPHY) return;
    col = room_trophy & 31;
    row = room_trophy >> 5;
    for (i = 0, s = sprites; i < sprite_count; i++, s++) {
        if (!s->visible) continue;
        // A sprite covers 3 columns of cells and 2 or 3 rows
        if (col >= (s->x >> 3) && col <= (s->x >> 3) + 2 &&
            row >= (s->y >> 3) && row <= (unsigned char)(s->y + 15) >> 3) return;
    }
    room_draw_trophy(want);
    trophy_drawn = want;
}

// One 50Hz step of game logic
static void game_tick(unsigned char held, unsigned char pressed) {
    unsigned char ev;

    if (state == STATE_DYING) {
        update_death();
        return;
    }
    if (state == STATE_CLEAR) {
        update_clear();
        return;
    }
    if (state != STATE_PLAY) return;

    // Skip straight to the next level, or finish the game on the last one
    if (pressed & IN_SKIP) {
        if (room_current + 1 == room_count) {
            state = STATE_WON;
        } else {
            start_level(room_current + 1);
        }
        return;
    }

    enemies_update();
    ev = player_update(held, pressed);
    if (!ev && touching_trophy()) {
        trophy_held = 1;
        room_set_trophy(0);
        hud_trophies(trophies, room_count, 1);
        beep(20, 300);
        beep(30, 200);
    }
    if (!ev && enemies_hit(FIX_WHOLE(player.x) + PLAYER_HURT_LEFT,
                           FIX_WHOLE(player.y) + PLAYER_HURT_TOP,
                           FIX_WHOLE(player.x) + PLAYER_HURT_RIGHT,
                           FIX_WHOLE(player.y) + PLAYER_HURT_BOTTOM)) {
        ev = EV_DIED;
    }

    if (ev) handle_event(ev);
}

// Play from the first level until the last one is finished
static void play_game(void) {
    unsigned char held;
    unsigned char prev;
    unsigned char pressed;
    unsigned char ticks;

    deaths = 0;
    trophies = 0;
    start_level(0);
    sync_sprites();
    // The jump that started the game shouldn't also make the player jump
    prev = input_read();

    while (state != STATE_WON) {
        // Sync to the frame interrupt, which fires at the start of the top
        // border. If last frame's work overran, this returns at once instead.
        ticks = wait_frame();
        if (frame_late) frame_overruns++;

        // Draw first, using last frame's state: the beam is still in the top
        // border, so sprites are updated before it reaches them.
        TIMING(frame_late ? INK_WHITE : INK_RED);
        sprites_render();
        update_trophy_cell();

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
        music_update();

        TIMING(INK_BLACK);
    }
}

int main(void) {
    interrupts_init();
    zx_border(INK_BLACK);
    render_init();
    input_init();
    music_init();

    for (;;) {
        title_screen();
        play_game();
        ending_screen(deaths, trophies, room_count);
    }
}
