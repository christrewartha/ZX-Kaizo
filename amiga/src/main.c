// The Amiga version: the same levels and physics as the Spectrum (player.c
// and enemy.c are shared), zoomed in to twice the size on a scrolling
// screen. Each level opens with a fly-over from the exit back to the
// start, since the whole level no longer fits on the screen.
#include "gcc8_c_support.h"
#include "fixed.h"
#include "input.h"
#include "player.h"
#include "enemy.h"
#include "render.h"             // the SPR_* frame numbers player.c uses
#include "room.h"
#include "roomgfx.h"
#include "hw.h"
#include "video.h"
#include "hud.h"
#include "sound.h"
#include "title.h"
#include "gfx_data.h"

// Build with -DDEBUG_START=<level> to start on that level
#ifndef DEBUG_START
#define DEBUG_START 0
#endif

#define STATE_FLYOVER 0
#define STATE_PLAY    1
#define STATE_DYING   2
#define STATE_CLEAR   3
#define STATE_WON     4           // left the last level

// Death sequence, in frames (as on the Spectrum)
#define DEATH_HITSTOP 12
#define DEATH_PAUSE   25

#define CLEAR_FRAMES  48
#define MAX_CATCHUP   2

// The fly-over: hold on the exit, then glide to the start. The level fades
// up during the hold.
#define FLY_HOLD      40
#define FLY_GLIDE     110
#define FADE_FRAMES   16        // brightness steps, one a frame

// The camera keeps the hero a little left or right of centre, looking
// ahead the way they face, and a little below the middle. It eases
// towards that point by 1/CAM_EASE of the way each frame.
#define LOOK_AHEAD    40
#define CAM_EASE      8
#define HERO_SCREEN_Y 124

static unsigned char state;
static unsigned short state_timer;
static unsigned char player_visible;
static unsigned char trophy_held;
static unsigned char trophies;
static unsigned int deaths;

// Last ticks player state, to hear jumps
static unsigned char was_jumping, was_wall_lock;
static unsigned char thunder;           // frames until the thunder
static unsigned char held_now;          // this frame's input (Foxy crouches)

// The camera, in 1/16ths of a world pixel
static int cam_x, cam_y;
static int look;                        // current look-ahead, pixels
static int fly_x, fly_y;                // where the fly-over starts

unsigned short frame_overruns;

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// World pixels: twice the Spectrum's, from 8.8 fixed point
static int world_x(void) { return player.x >> 7; }
static int world_y(void) { return player.y >> 7; }

// Where the camera wants to be (top-left, whole pixels)
static void camera_target(int *tx, int *ty) {
    *tx = clamp(world_x() + 16 - VIEW_W / 2 + look, 0, CAM_MAX_X);
    *ty = clamp(world_y() + 16 - HERO_SCREEN_Y, 0, CAM_MAX_Y);
}

static void respawn(void) {
    const room_t *room = &rooms[room_current];

    if (trophy_held) {
        trophy_held = 0;
        room_set_trophy(1);
        room_draw_trophy(1);
        hud_trophies(trophies, room_count, 0);
    }
    enemies_load(room->enemies, room->enemy_count);
    player.facing_left = 0;
    player_spawn(room->start_x, room->start_y);
    player_visible = 1;
    was_jumping = 0;
    was_wall_lock = 0;
    state = STATE_PLAY;
}

// Fade the screen out, a step a frame
static void fade_out(void) {
    for (int b = FADE_FRAMES - 1; b >= 0; b--) {
        wait_frame();
        video_brightness(b);
    }
}

static void start_level(unsigned char n) {
    int cell;

    video_hero(0, 0, -1);
    video_theme(n);             // first: the room is drawn in its style
    room_load(n);
    video_brightness(0);
    hud_level(rooms[n].name, n + 1, room_count);
    hud_deaths(deaths);
    hud_trophies(trophies, room_count, 0);
    trophy_held = 0;
    respawn();
    look = LOOK_AHEAD;

    // The fly-over starts on the exit
    cell = room_exit_cell();
    fly_x = clamp((cell & 31) * 16 + 8 - VIEW_W / 2, 0, CAM_MAX_X);
    fly_y = clamp((cell >> 5) * 16 - VIEW_H / 2, 0, CAM_MAX_Y);
    cam_x = fly_x << 4;
    cam_y = fly_y << 4;
    state = STATE_FLYOVER;
    state_timer = 0;

    // Drawing the level took several frames: restart the frame timing
    // rather than count them as an overrun to catch up on
    wait_frame();
}

static void next_level(void) {
    fade_out();
    if (room_current + 1 == room_count) state = STATE_WON;
    else start_level(room_current + 1);
}

static void start_death(void) {
    state = STATE_DYING;
    state_timer = 0;
    player_die();
    sound_play(SFX_DEATH);
}

static void update_death(void) {
    ++state_timer;
    if (state_timer == DEATH_HITSTOP) {
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

static void handle_event(unsigned char ev) {
    switch (ev) {
        case EV_FELL:
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
            room_open_door();
            sound_play(SFX_CLEAR);
            break;
    }
}

// Any cell under the whole sprite, not just the collision box: some gems
// sit where only the top of a full-height (16 pixel) hero could reach
static unsigned char touching_trophy(void) {
    unsigned char left = FIX_WHOLE(player.x) + PLAYER_HB_X;
    unsigned char top = FIX_WHOLE(player.y);
    unsigned char c0 = left >> 3, c1 = (unsigned char)(left + PLAYER_HB_W - 1) >> 3;
    unsigned char r0 = top >> 3, r1 = (unsigned char)(top + PLAYER_HEIGHT - 1) >> 3;

    for (unsigned char r = r0; r <= r1; r++) {
        for (unsigned char c = c0; c <= c1; c++) {
            if (room_coll[((unsigned int)r << 5) | c] & TILE_TROPHY) return 1;
        }
    }
    return 0;
}

// Smoothstep from 0 to 4096 as t goes 0..n
static int ease(int t, int n) {
    int s = (t << 12) / n;
    return (s * s >> 12) * (3 * 4096 - 2 * s) >> 12;
}

static void game_tick(unsigned char held, unsigned char pressed) {
    unsigned char ev;
    int tx, ty;

    switch (state) {
        case STATE_FLYOVER:
            // Fire skips it
            if (pressed & IN_JUMP) state_timer = FLY_HOLD + FLY_GLIDE;
            state_timer++;
            video_brightness(state_timer < FADE_FRAMES ? state_timer : FADE_FRAMES);
            if (state_timer > FLY_HOLD) {
                int t = state_timer - FLY_HOLD, e;
                if (t > FLY_GLIDE) t = FLY_GLIDE;
                e = ease(t, FLY_GLIDE);
                camera_target(&tx, &ty);
                cam_x = (fly_x << 4) + ((((tx - fly_x) << 4) * e) >> 12);
                cam_y = (fly_y << 4) + ((((ty - fly_y) << 4) * e) >> 12);
                if (t == FLY_GLIDE) state = STATE_PLAY;
            }
            return;
        case STATE_DYING:
            update_death();
            return;
        case STATE_CLEAR:
            if (++state_timer == CLEAR_FRAMES) next_level();
            return;
    }

    enemies_update();
    ev = player_update(held, pressed);
    if (!ev) {
        if (player.wall_lock && !was_wall_lock) sound_play(SFX_WALL_JUMP);
        else if (player.jumping && !was_jumping) sound_play(SFX_JUMP);
        was_jumping = player.jumping;
        was_wall_lock = player.wall_lock;
    }
    if (!ev && touching_trophy()) {
        trophy_held = 1;
        room_set_trophy(0);
        room_draw_trophy(0);
        hud_trophies(trophies, room_count, 1);
        sound_play(SFX_TROPHY);
    }
    if (!ev && enemies_hit(FIX_WHOLE(player.x) + PLAYER_HURT_LEFT,
                           FIX_WHOLE(player.y) + PLAYER_HURT_TOP,
                           FIX_WHOLE(player.x) + PLAYER_HURT_RIGHT,
                           FIX_WHOLE(player.y) + PLAYER_HURT_BOTTOM)) {
        ev = EV_DIED;
    }
    if (ev) handle_event(ev);
}

static void update_camera(void) {
    int tx, ty;
    int want = player.facing_left ? -LOOK_AHEAD : LOOK_AHEAD;

    if (state == STATE_FLYOVER) return;
    look += (want - look) / 16;
    camera_target(&tx, &ty);
    cam_x += ((tx << 4) - cam_x) / CAM_EASE;
    cam_y += ((ty << 4) - cam_y) / CAM_EASE;
}

// Foxy's frame (HERO_*; + 1 is the same facing left), or -1 for none
static int hero_frame(void) {
    static const unsigned char idle[4] = { HERO_IDLE_1, HERO_IDLE_2, HERO_IDLE_3, HERO_IDLE_4 };
    static const unsigned char run[6] = { HERO_RUN_1, HERO_RUN_2, HERO_RUN_3, HERO_RUN_4, HERO_RUN_5, HERO_RUN_6 };
    unsigned char f = player.frame, left = 0, base;

    if (!player_visible) return -1;
    if (f == SPR_DEAD) {
        // Flickering while the game holds still, then knocked away
        if (state == STATE_DYING && state_timer < DEATH_HITSTOP && (state_timer & 2)) return -1;
        return ((frame_count >> 3) & 1) ? HERO_HURT_2 : HERO_HURT_1;
    }
    if (f >= SPR_LEFT) {
        f -= SPR_LEFT;
        left = 1;
    }
    if (state == STATE_CLEAR) return HERO_VICTORY + left;
    switch (f) {
        case SPR_IDLE:  base = (held_now & IN_DOWN) ? HERO_CROUCH : idle[(frame_count >> 3) & 3]; break;
        case SPR_RUN_A:
        case SPR_RUN_B: base = run[(player.anim >> 10) % 6]; break;
        case SPR_JUMP:  base = player.vy < 0 ? HERO_JUMP : HERO_FALL; break;
        default:        base = ((frame_count >> 3) & 1) ? HERO_WALL_2 : HERO_WALL_1; break;
    }
    return base + left;
}

// The enemies, drawn by the blitter into the hidden buffer: the opossum
// runs (its art faces left; _R faces right), the eagle flaps
static void draw_enemies(void) {
    enemy_t *e = enemies;
    unsigned char i, frame;

    video_bobs_begin();
    for (i = 0; i < enemy_count; i++, e++) {
        if (e->type == ENEMY_WALKER) {
            frame = BOB_WALKER_1 + ((e->anim >> 9) % 6) * 2 + (e->vel > 0);
        } else {
            frame = BOB_FLYER_1 + (((frame_count >> 2) + i) & 3);
        }
        video_bob(e->x >> 7, e->y >> 7, frame);
    }
    video_bobs_end();
}

// Play from the first level to the last; returns 0 if the mouse button
// quit instead (for development)
static int play_game(void) {
    unsigned char held, prev, pressed;
    UWORD ticks;
    int hero_x = 0, hero_y = 0, frame = -1;

    deaths = 0;
    trophies = 0;
    start_level(DEBUG_START ? DEBUG_START - 1 : 0);
    prev = input_read();

    while (state != STATE_WON) {
        if (mouse_left()) return 0;
        ticks = wait_frame();
        if (frame_late) frame_overruns++;

        // The copper picks these up a little into the vertical blank
        video_scroll(cam_x >> 4, cam_y >> 4);
        video_hero(hero_x, hero_y, frame);
        if (video_animate()) thunder = 12;
        if (thunder && !--thunder) sound_play(SFX_THUNDER);
        hud_animate();
        room_animate(frame_count);

        held = input_read();
        held_now = held;
        pressed = held & ~prev;
        prev = held;
        if (ticks > MAX_CATCHUP) ticks = MAX_CATCHUP;
        do {
            game_tick(held, pressed);
            pressed = 0;
        } while (--ticks && state != STATE_WON);

        update_camera();
        draw_enemies();
        frame = hero_frame();
        hero_x = world_x() - (cam_x >> 4);
        hero_y = world_y() - (cam_y >> 4);
    }
    return 1;
}

int main(void) {
    hw_init();
    hud_init();             // before video_init(): the copper list points at the bar
    video_init();
    hw_take();
    sound_init();
    input_init();
    video_start();

    for (;;) {
        if (!DEBUG_START) title_screen();
        if (!play_game()) break;
        ending_screen(deaths, trophies, room_count);
    }

    hw_free();
    return 0;
}
