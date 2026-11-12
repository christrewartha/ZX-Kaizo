// Kaiz64o: the Commodore 64 version of KaiZXo. The game logic (player.c,
// enemy.c) and the levels are shared with the Spectrum; everything you see
// and hear is the C64's own.
#include "fixed.h"
#include "input.h"
#include "player.h"
#include "enemy.h"
#include "render.h"             // the SPR_* frame numbers player.c uses
#include "room.h"
#include "hw.h"
#include "irq.h"
#include "gfx.h"
#include "spr.h"
#include "panel.h"
#include "sound.h"
#include "screens.h"
#include "sprite_data.h"

extern const signed char sine64[64];

#define STATE_PLAY  0
#define STATE_DYING 1
#define STATE_CLEAR 2           // reached the exit
#define STATE_WON   3           // reached the last level's exit

// Death sequence, in frames (as on the Spectrum)
#define DEATH_HITSTOP 12        // frozen, flashing, the screen shaking
#define DEATH_PAUSE   25        // empty screen after falling off the bottom

// Standing at the exit while the fanfare plays
#define CLEAR_FRAMES  48

// Most logic steps run in one frame to catch up after an overrun
#define MAX_CATCHUP   2

#define PLAYER_COLOUR VCOL_CYAN

// Build with -DDEBUG_START=<level> to skip the title and start on that level
#ifndef DEBUG_START
#define DEBUG_START 0
#endif

static unsigned char state;
static unsigned char state_timer;
static unsigned char player_visible;
static unsigned char player_colour;
static unsigned int deaths;

// Trophies won so far, and whether this level's is being carried
static unsigned char trophies;
static unsigned char trophy_held;

// Last tick's player state, to spot jumps and landings
static unsigned char was_jumping;
static unsigned char was_on_ground;
static unsigned char was_wall_lock;

// Frames where the work didn't finish in time (watch it in a debugger)
unsigned int frame_overruns;

// Spectrum ink (enemy_def.colour & 7) to a C64 sprite colour
static const unsigned char enemy_colours[8] = {
    VCOL_DARK_GREY, VCOL_LT_BLUE, VCOL_LT_RED, VCOL_PURPLE,
    VCOL_LT_GREEN, VCOL_CYAN, VCOL_YELLOW, VCOL_LT_GREY
};

static void respawn(void) {
    const room_t *room = &rooms[room_current];

    if (trophy_held) {
        trophy_held = 0;
        room_set_trophy(1);
        panel_trophies(trophies, room_count, 0);
    }
    enemies_load(room->enemies, room->enemy_count);
    player.facing_left = 0;
    player_spawn(room->start_x, room->start_y);
    player_visible = 1;
    player_colour = PLAYER_COLOUR;
    was_jumping = 0;
    was_on_ground = 1;
    was_wall_lock = 0;
    state = STATE_PLAY;
}

static void start_level(unsigned char n) {
    spr_hide();
    fx_clear();
    panel_level(rooms[n].name, n + 1, room_count);
    panel_deaths(deaths);
    panel_trophies(trophies, room_count, 0);
    trophy_held = 0;
    room_load(n);
    respawn();
    // The wipe took a while: restart the frame timing rather than count
    // that as an overrun to catch up on
    wait_frame();
}

static void next_level(void) {
    if (room_current + 1 == room_count) {
        state = STATE_WON;
    } else {
        start_level(room_current + 1);
    }
}

static void start_death(void) {
    state = STATE_DYING;
    state_timer = 0;
    player_die();
    sound_play(SFX_DEATH);
    gfx_shake(DEATH_HITSTOP + 4);
}

// Everything else is frozen while this plays out
static void update_death(void) {
    ++state_timer;
    if (state_timer < DEATH_HITSTOP) {
        player_colour = (state_timer & 2) ? VCOL_RED : VCOL_WHITE;
    } else if (state_timer == DEATH_HITSTOP) {
        player_colour = VCOL_RED;
        player_death_pop();
    } else if (player_visible) {
        // (oscar64 1.32 wrongly folds player_death_fall()'s result, a test
        // of FIX_WHOLE(y), to 0, so the body fell forever. The same test on
        // the whole 16-bit position compiles correctly.)
        player_death_fall();
        if (player.y >= FIX_FROM_INT(ROOM_BOTTOM_Y + 1)) {
            player_visible = 0;
            state_timer = DEATH_HITSTOP;
        }
    } else if (state_timer == DEATH_HITSTOP + DEATH_PAUSE) {
        panel_deaths(++deaths);
        respawn();
    }
}

// The player stands at the exit while sparkles burst around them
static void update_clear(void) {
    ++state_timer;
    if (!(state_timer & 7)) {
        fx_spawn(FX_SPARK, FIX_WHOLE(player.x) + (rnd() & 15) - 8, FIX_WHOLE(player.y) + (rnd() & 15) - 12);
    }
    if (state_timer == CLEAR_FRAMES) next_level();
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
            if (trophy_held) panel_trophies(++trophies, room_count, 1);
            state = STATE_CLEAR;
            state_timer = 0;
            player.frame = SPR_IDLE + (player.facing_left ? SPR_LEFT : 0);
            sound_play(SFX_CLEAR);
            break;
    }
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

// Sounds and dust for what the player just did
static void player_effects(void) {
    int x = FIX_WHOLE(player.x);
    int y = FIX_WHOLE(player.y);

    if (player.wall_lock && !was_wall_lock) {
        sound_play(SFX_WALL_JUMP);
        fx_spawn(FX_PUFF, x + (player.facing_left ? 6 : -6), y - 2);
    } else if (player.jumping && !was_jumping) {
        sound_play(SFX_JUMP);
        fx_spawn(FX_PUFF, x, y + 1);
    } else if (player.on_ground && !was_on_ground) {
        fx_spawn(FX_PUFF, x, y + 1);
    }
    was_jumping = player.jumping;
    was_on_ground = player.on_ground;
    was_wall_lock = player.wall_lock;
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
        next_level();
        return;
    }

    enemies_update();
    ev = player_update(held, pressed);
    if (!ev) player_effects();
    if (!ev && touching_trophy()) {
        trophy_held = 1;
        room_set_trophy(0);
        panel_trophies(trophies, room_count, 1);
        sound_play(SFX_TROPHY);
        fx_spawn(FX_SPARK, (room_trophy & 31) * 8 - 4, (room_trophy >> 5) * 8 - 4);
    }
    if (!ev && enemies_hit(FIX_WHOLE(player.x) + PLAYER_HURT_LEFT,
                           FIX_WHOLE(player.y) + PLAYER_HURT_TOP,
                           FIX_WHOLE(player.x) + PLAYER_HURT_RIGHT,
                           FIX_WHOLE(player.y) + PLAYER_HURT_BOTTOM)) {
        ev = EV_DIED;
    }

    if (ev) handle_event(ev);
}

// The player's sprite images: SP_<art> + 1 faces left, + 2 is the outline
static unsigned char player_image(void) {
    static const unsigned char run[4] = { SP_P_RUN_1, SP_P_RUN_2, SP_P_RUN_3, SP_P_RUN_4 };
    unsigned char f = player.frame, left = 0, base;

    if (f == SPR_DEAD) return SP_P_DEAD;
    if (f >= SPR_LEFT) {
        f -= SPR_LEFT;
        left = 1;
    }
    switch (f) {
        case SPR_IDLE:
            base = ((frame_count & 127) < 5) ? SP_P_BLINK : SP_P_IDLE;
            break;
        case SPR_RUN_A:
        case SPR_RUN_B:
            base = run[(player.anim >> 10) & 3];
            break;
        case SPR_JUMP:
            base = player.vy < 0 ? SP_P_JUMP : SP_P_FALL;
            break;
        default:
            base = SP_P_WALL;
            break;
    }
    return base + left;
}

// Copy game state into the sprite shadows for the next frame
static void sync_sprites(void) {
    static const unsigned char flap[4] = { SP_FLYER_1, SP_FLYER_2, SP_FLYER_3, SP_FLYER_2 };
    hwsprite_t *s;
    enemy_t *e = enemies;
    unsigned char i, img, k;
    int x = FIX_WHOLE(player.x), y = FIX_WHOLE(player.y);
    signed char bob;

    img = player_image();
    s = &hw[SLOT_PLAYER];
    s->x = x;
    s->y = y;
    s->image = img;
    s->colour = player_colour;
    s->on = player_visible;
    s = &hw[SLOT_OUTLINE];
    s->x = x;
    s->y = y;
    // The dead pose has no left-facing copy, so its outline is next
    s->image = img + (img == SP_P_DEAD ? 1 : 2);
    s->colour = VCOL_BLACK;
    s->on = player_visible;

    for (i = 0; i < 3; i++, e++) {
        s = &hw[SLOT_ENEMY + i];
        s->on = i < enemy_count;
        if (!s->on) continue;
        s->x = FIX_WHOLE(e->x);
        s->y = FIX_WHOLE(e->y);
        if (e->type == ENEMY_WALKER) {
            k = e->frame - SPR_WALKER_A;        // A, B, then the same facing left
            s->image = ((k & 1) ? SP_WALKER_2 : SP_WALKER_1) + (k >> 1);
        } else {
            s->image = flap[((frame_count >> 2) + i) & 3];
        }
        s->colour = enemy_colours[e->colour & 7];
    }

    // The trophy bobs in its place, or over the player's head when carried
    s = &hw[SLOT_TROPHY];
    s->on = room_trophy != NO_TROPHY;
    bob = sine64[(frame_count << 1) & 63] >> 6;
    if (trophy_held) {
        s->x = x;
        s->y = y - 13 + bob;
        s->on = player_visible;
    } else {
        s->x = (room_trophy & 31) * 8 - 4;
        s->y = (room_trophy >> 5) * 8 - 1 + bob;
    }
    s->image = ((frame_count & 63) < 6) ? SP_CUP_SHINE : SP_CUP;
    s->colour = VCOL_YELLOW;
}

// Play from the first level until the last one is finished
static void play_game(void) {
    unsigned char held, prev, pressed, ticks;

    deaths = 0;
    trophies = 0;
    gfx_game_splits();
    start_level(DEBUG_START ? DEBUG_START - 1 : 0);
    sync_sprites();
    // The jump that started the game shouldn't also make the player jump
    prev = input_read();

    while (state != STATE_WON) {
        ticks = wait_frame();
        if (frame_late) frame_overruns++;

        // Sprites first, while the beam is in the border, from last
        // frame's state
        spr_commit(gfx_shake_x);
        gfx_animate();
        panel_animate();

        held = input_read();
        pressed = held & ~prev;
        prev = held;
        if (ticks > MAX_CATCHUP) ticks = MAX_CATCHUP;
        do {
            game_tick(held, pressed);
            pressed = 0;
        } while (--ticks);
        sync_sprites();
        fx_update();
    }
    spr_hide();
}

int main(void) {
    gfx_init();
    spr_init();
    input_init();
    sound_init();
    gfx_game_splits();
    irq_init();

    for (;;) {
        if (!DEBUG_START) title_screen();
        play_game();
        ending_screen(deaths, trophies, room_count);
    }
    return 0;
}
