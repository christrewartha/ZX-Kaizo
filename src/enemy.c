#include "enemy.h"
#include "render.h"

enemy_t enemies[MAX_ENEMIES];
unsigned char enemy_count;

static unsigned char enemy_tick;

// Hit box per type, relative to the sprite's top-left, fitted to the art
static const unsigned char hit_left[2]   = { 2,  1 };
static const unsigned char hit_right[2]  = { 13, 14 };
static const unsigned char hit_top[2]    = { 4,  3 };
static const unsigned char hit_bottom[2] = { 15, 10 };

static void set_frame(enemy_t *e) {
    if (e->type == ENEMY_WALKER) {
        // Step every 4 pixels travelled
        e->frame = ((e->anim & 0x0400) ? SPR_WALKER_B : SPR_WALKER_A) +
                   ((e->vel < 0) ? SPR_WALKER_LEFT : 0);
    } else {
        e->frame = (enemy_tick & 4) ? SPR_FLYER_B : SPR_FLYER_A;
    }
}

void enemies_load(const enemy_def *def, unsigned char count) {
    enemy_t *e = enemies;

    enemy_count = count;
    while (count--) {
        e->type = def->type;
        e->x = FIX_FROM_INT(def->x);
        e->y = FIX_FROM_INT(def->y);
        e->vel = def->speed;
        e->min = def->min;
        e->max = def->max;
        e->colour = def->colour;
        e->anim = 0;
        set_frame(e);
        ++e;
        ++def;
    }
}

void enemies_update(void) {
    enemy_t *e = enemies;
    ufix8 *axis;
    unsigned char i, pos;

    enemy_tick++;
    for (i = enemy_count; i; --i, ++e) {
        axis = (e->type == ENEMY_WALKER) ? &e->x : &e->y;
        *axis += e->vel;
        pos = FIX_WHOLE(*axis);

        if (e->vel > 0) {
            if (pos >= e->max) {
                *axis = FIX_FROM_INT(e->max);
                e->vel = -e->vel;
            }
        } else if (pos <= e->min) {
            *axis = FIX_FROM_INT(e->min);
            e->vel = -e->vel;
        }

        e->anim += (e->vel < 0) ? -e->vel : e->vel;
        set_frame(e);
    }
}

unsigned char enemies_hit(unsigned char left, unsigned char top, unsigned char right, unsigned char bottom) {
    enemy_t *e = enemies;
    unsigned char i, ex, ey, t;

    for (i = enemy_count; i; --i, ++e) {
        ex = FIX_WHOLE(e->x);
        ey = FIX_WHOLE(e->y);
        t = e->type;
        if (right >= ex + hit_left[t] && left <= ex + hit_right[t] &&
            bottom >= ey + hit_top[t] && top <= ey + hit_bottom[t]) {
            return 1;
        }
    }
    return 0;
}
