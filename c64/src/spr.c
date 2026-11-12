#include <string.h>
#include "spr.h"
#include "hw.h"
#include "sprite_data.h"

hwsprite_t hw[8];

void spr_init(void) {
    memcpy(SPRITE_RAM, sprite_data, SP_COUNT * 64);
    vic.spr_mcolor0 = VCOL_DARK_GREY;   // 'k' in sprites.txt: black would vanish on black
    vic.spr_mcolor1 = VCOL_WHITE;       // 'w'
    vic.spr_multi = 0xFF & ~(1 << SLOT_OUTLINE);
    vic.spr_priority = 0;               // all in front of the background
    vic.spr_expand_x = 0;
    vic.spr_expand_y = 0;
    spr_hide();
}

void spr_hide(void) {
    unsigned char i;

    for (i = 0; i < 8; i++) hw[i].on = 0;
    vic.spr_enable = 0;
}

void spr_commit(unsigned char dx) {
    unsigned char i, bit = 1, msb = 0, enable = 0;
    int x, y;
    hwsprite_t *s = hw;

    for (i = 0; i < 8; i++, s++, bit <<= 1) {
        if (!s->on) continue;
        x = s->x + SPR_X0 + dx;
        y = s->y + SPR_Y0;
        if (x < 0 || x > 343 || y < 0 || y > 255) continue;
        vic.spr_pos[i].x = (unsigned char)x;
        if (x & 0x100) msb |= bit;
        vic.spr_pos[i].y = (unsigned char)y;
        vic.spr_color[i] = s->colour;
        SPRITE_PTRS[i] = SPRITE_PTR0 + s->image;
        enable |= bit;
    }
    vic.spr_msbx = msb;
    vic.spr_enable = enable;
}

// ------------------------------------------------------------ particles

#define FX_FRAMES   4
#define FX_TICKS    4           // frames each image shows

typedef struct {
    int x, y;
    unsigned char kind;
    unsigned char age;          // 0 = free
} fx_t;

static fx_t fx[FX_SLOTS];
static unsigned char fx_next;

static const unsigned char fx_first[2] = { SP_PUFF_1, SP_SPARK_1 };
static const unsigned char fx_colour[2] = { VCOL_LT_GREY, VCOL_YELLOW };

void fx_spawn(unsigned char kind, int x, int y) {
    fx_t *f = &fx[fx_next];

    fx_next = (fx_next + 1) % FX_SLOTS;
    f->x = x;
    f->y = y;
    f->kind = kind;
    f->age = 1;
}

void fx_update(void) {
    unsigned char i;
    fx_t *f = fx;
    hwsprite_t *s = &hw[SLOT_FX];

    for (i = 0; i < FX_SLOTS; i++, f++, s++) {
        s->on = 0;
        if (!f->age) continue;
        if (f->age > FX_FRAMES * FX_TICKS) {
            f->age = 0;
            continue;
        }
        s->on = 1;
        s->x = f->x;
        s->y = f->y;
        s->image = fx_first[f->kind] + (f->age - 1) / FX_TICKS;
        s->colour = fx_colour[f->kind];
        f->age++;
    }
}

void fx_clear(void) {
    fx[0].age = 0;
    fx[1].age = 0;
}
